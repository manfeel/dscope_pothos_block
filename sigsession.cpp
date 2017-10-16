/*
 * This file is part of the DSView project.
 * DSView is based on PulseView.
 *
 * Copyright (C) 2012 Joel Holdsworth <joel@airwebreathe.org.uk>
 * Copyright (C) 2013 DreamSourceLab <support@dreamsourcelab.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301 USA
 */
#include "sigsession.h"
#include "devicemanager.h"
#include "device.h"

#include <assert.h>
#include <stdexcept>
#include <sys/stat.h>

#include <boost/foreach.hpp>

//using boost::dynamic_pointer_cast;
//using boost::function;
//using boost::lock_guard;
//using boost::mutex;
//using boost::shared_ptr;
//using std::list;
//using std::map;
//using std::set;
//using std::string;
//using std::vector;
//using std::deque;
//using std::min;

using namespace boost;
using namespace std;

//namespace pv {

// TODO: This should not be necessary
SigSession* SigSession::_session = NULL;

SigSession::SigSession(DeviceManager &device_manager, BlockingQueue<sr_datafeed_dso> &dso_queue) :
	_device_manager(device_manager),
    _dso_queue(dso_queue),
    _capture_state(Init),
    _instant(false),
    _error(No_err),
    _run_mode(Single),
    _repeat_intvl(1),
    _repeating(false),
    _repeat_hold_prg(0),
    _map_zoom(0)
{
	// TODO: This should not be necessary
	_session = this;
    _hot_attach = false;
    _hot_detach = false;
    _group_cnt = 0;
    _noData_cnt = 0;
    _data_lock = false;
    _data_updated = false;

    //_cur_dso_snapshot.reset(new DsoSnapshot());
    //_dso_data.reset(new Dso());
    //_dso_data->push_snapshot(_cur_dso_snapshot);
}

SigSession::~SigSession()
{
	stop_capture();
		       
    ds_trigger_destroy();

    _dev_inst->release();

	// TODO: This should not be necessary
	_session = NULL;
}

boost::shared_ptr<DevInst> SigSession::get_device() const
{
    return _dev_inst;
}

void SigSession::set_device(boost::shared_ptr<DevInst> dev_inst)
{
    //using pv::device::Device;

    // Ensure we are not capturing before setting the device
    //stop_capture();

    if (_dev_inst) {
        sr_session_datafeed_callback_remove_all();
        _dev_inst->release();
    }

    _dev_inst = dev_inst;

    if (_dev_inst) {
        try {
            _dev_inst->use(this);
            _cur_samplerate = _dev_inst->get_sample_rate();
            _cur_samplelimits = _dev_inst->get_sample_limit();

            if (_dev_inst->dev_inst()->mode == DSO)
                set_run_mode(Repetitive);
            else
                set_run_mode(Single);
        } catch(std::exception) {
            std::cout << "set_device failed!" << std::endl;
            return;
        }
        sr_session_datafeed_callback_add(data_feed_in_proc, NULL);
    }
}


void SigSession::set_default_device()
{
    boost::shared_ptr<DevInst> default_device;
    const list<boost::shared_ptr<DevInst> > &devices =
        _device_manager.devices();

    if (!devices.empty()) {
        // Fall back to the first device in the list.
        default_device = devices.front();

        // Try and find the DreamSourceLab device and select that by default
        BOOST_FOREACH (boost::shared_ptr<DevInst> dev, devices)
            if (dev->dev_inst() &&
                    (dev->name().find("virtual") == std::string::npos)) {
                default_device = dev;
                break;
            }
        try {
            set_device(default_device);
        } catch(std::exception e) {
            std::cout << "set device error!" << std::endl;
            return;
        }
    }
}

void SigSession::release_device(DevInst *dev_inst)
{
    (void)dev_inst;
    assert(_dev_inst.get() == dev_inst);

    assert(get_capture_state() != Running);
    _dev_inst = boost::shared_ptr<DevInst>();
    //_dev_inst.reset();
}

SigSession::capture_state SigSession::get_capture_state() const
{
    boost::lock_guard<boost::mutex> lock(_sampling_mutex);
	return _capture_state;
}

uint64_t SigSession::cur_samplelimits() const
{
    return _cur_samplelimits;
}

uint64_t SigSession::cur_samplerate() const
{
    return _cur_samplerate;
}

double SigSession::cur_sampletime() const
{
    if (_cur_samplerate == 0)
        return 0;
    else
        return  cur_samplelimits() * 1.0 / cur_samplerate();
}

void SigSession::set_cur_samplerate(uint64_t samplerate)
{
    assert(samplerate != 0);
    _cur_samplerate = samplerate;
    // TODO: populate samplerate to real device
    //if (_dso_data)
    //    _dso_data->set_samplerate(_cur_samplerate);
}

void SigSession::set_cur_samplelimits(uint64_t samplelimits)
{
    assert(samplelimits != 0);
    _cur_samplelimits = samplelimits;
    // TODO: populate samplelimits to real device
}


void SigSession::init_signals()
{
    assert(_dev_inst);
    stop_capture();

    unsigned int logic_probe_count = 0;
    unsigned int dso_probe_count = 0;
    unsigned int analog_probe_count = 0;

    //if (_dso_data)
    //    _dso_data->clear();

    // Detect what data types we will receive
    if(_dev_inst) {
        assert(_dev_inst->dev_inst());
        for (const GSList *l = _dev_inst->dev_inst()->channels;
             l; l = l->next) {
            const sr_channel *const probe = (const sr_channel *)l->data;

            switch(probe->type) {
                case SR_CHANNEL_LOGIC:
                    if(probe->enabled)
                        logic_probe_count++;
                    break;

                case SR_CHANNEL_DSO:
                    dso_probe_count++;
                    break;

                case SR_CHANNEL_ANALOG:
                    if(probe->enabled)
                        analog_probe_count++;
                    break;
            }
        }
    }
}

void SigSession::capture_init()
{
    _cur_samplerate = _dev_inst->get_sample_rate();
    _cur_samplelimits = _dev_inst->get_sample_limit();
    _data_updated = false;
    _trigger_flag = false;
    _hw_replied = false;
    _noData_cnt = 0;
    //
    //if (_dso_data) {
    //    _dso_data->init();
    //    _dso_data->set_samplerate(_cur_samplerate);
    //}
}


void SigSession::start_capture(bool instant)
{
    // Check that a device instance has been selected.
    if (!_dev_inst) {
        std::cout << "No device selected";
        return;
    }
    assert(_dev_inst->dev_inst());

    if (!_dev_inst->is_usable()) {
        _error = Hw_err;
        return;
    }

    // stop previous capture
	stop_capture();

    // update setting
    if (_dev_inst->name() != "virtual-session")
        _instant = instant;
    else
        _instant = true;
    capture_init();

	// Check that at least one probe is enabled
	const GSList *l;
    for (l = _dev_inst->dev_inst()->channels; l; l = l->next) {
        sr_channel *const probe = (sr_channel*)l->data;
		assert(probe);
		if (probe->enabled)
			break;
	}
	if (!l) {
		std::cout << "No probes enabled." << std::endl;
		return;
	}

	// Begin the session
	_sampling_thread.reset(new boost::thread(
        &SigSession::sample_thread_proc, this, _dev_inst));
}

void SigSession::stop_capture()
{
    _instant = false;

    if (get_capture_state() != Running)
		return;
	sr_session_stop();

	// Check that sampling stopped
    if (_sampling_thread.get())
        _sampling_thread->join();
    _sampling_thread.reset();
}

bool SigSession::get_capture_status(bool &triggered, int &progress)
{
    uint64_t sample_limits = cur_samplelimits();
    sr_status status;
    if (sr_status_get(_dev_inst->dev_inst(), &status, true, SR_STATUS_TRIG_BEGIN, SR_STATUS_TRIG_END) == SR_OK){
        triggered = status.trig_hit & 0x01;
        uint64_t captured_cnt = status.trig_hit >> 2;
        captured_cnt = ((uint64_t)status.captured_cnt0 +
                       ((uint64_t)status.captured_cnt1 << 8) +
                       ((uint64_t)status.captured_cnt2 << 16) +
                       ((uint64_t)status.captured_cnt3 << 24) +
                       (captured_cnt << 32));
        if (_dev_inst->dev_inst()->mode == DSO)
            //captured_cnt = captured_cnt * _signals.size() / get_ch_num(SR_CHANNEL_DSO);
        if (triggered)
            progress = (sample_limits - captured_cnt) * 100.0 / sample_limits;
        else
            progress = captured_cnt * 100.0 / sample_limits;
        return true;
    }
    return false;
}

bool SigSession::get_instant()
{
    return _instant;
}

void SigSession::set_capture_state(capture_state state)
{
    boost::lock_guard<boost::mutex> lock(_sampling_mutex);
	_capture_state = state;
}

void SigSession::sample_thread_proc(boost::shared_ptr<DevInst> dev_inst)
{
    assert(dev_inst);
    assert(dev_inst->dev_inst());
    //std::cout << "in func :" << __func__ << std::endl;
    try {
        dev_inst->start();
    } catch(std::exception) {
        //error_handler(e);
        std::cout << "dev_inst start failed!" << std::endl;
        return;
    }

    set_capture_state(Running);

    dev_inst->run();

    set_capture_state(Stopped);
}

void SigSession::data_feed_in(const struct sr_dev_inst *sdi,
    const struct sr_datafeed_packet *packet)
{
	assert(sdi);
	assert(packet);

    if (_data_lock)
        return;
    if (packet->type != SR_DF_END &&
        packet->status != SR_PKT_OK) {
        _error = Pkt_data_err;
        return;
    }

	switch (packet->type) {
	case SR_DF_HEADER:
		//feed_in_header(sdi);
		break;

	case SR_DF_META:
		assert(packet->payload);
		//feed_in_meta(sdi, *(const sr_datafeed_meta*)packet->payload);
		break;

    case SR_DF_TRIGGER:
        assert(packet->payload);
        //feed_in_trigger(*(const ds_trigger_pos*)packet->payload);
        break;

	case SR_DF_LOGIC:
		assert(packet->payload);
        //feed_in_logic(*(const sr_datafeed_logic*)packet->payload);
		break;

    case SR_DF_DSO:
        assert(packet->payload);
        feed_in_dso(*(const sr_datafeed_dso*)packet->payload);
        break;

	case SR_DF_ANALOG:
		assert(packet->payload);
        //feed_in_analog(*(const sr_datafeed_analog*)packet->payload);
		break;

    case SR_DF_OVERFLOW:
    {
        if (_error == No_err) {
            _error = Data_overflow;
//            session_error();
        }
        break;
    }
	case SR_DF_END:
	{
        //_cur_dso_snapshot->capture_ended();
        if (packet->status != SR_PKT_OK) {
            _error = Pkt_data_err;
//            session_error();
        }
//        frame_ended();
		break;
	}
	}
}

void SigSession::data_feed_in_proc(const struct sr_dev_inst *sdi,
    const struct sr_datafeed_packet *packet, void *cb_data)
{
	(void) cb_data;
	assert(_session);
	_session->data_feed_in(sdi, packet);
}


void SigSession::feed_in_dso(const sr_datafeed_dso &dso) {
    //std::cout << dso.num_samples << std::endl;
    _dso_queue.put(dso);
}

/*
void SigSession::feed_in_dso(const sr_datafeed_dso &dso)
{
    //boost::lock_guard<boost::mutex> lock(_data_mutex);

    if(!_dso_data || _cur_dso_snapshot->memory_failed())
    {
        cout << "Unexpected dso packet" << endl;
        return;	// This dso packet was not expected.
    }

    if (_cur_dso_snapshot->last_ended())
    {
        std::map<int, bool> sig_enable;
        // reset scale of dso signal
        for (const GSList *l = _dev_inst->dev_inst()->channels; l; l = l->next) {
            sr_channel *p = (sr_channel *)l->data;
            assert(p);
            sig_enable[p->index] = p->enabled;
        }
        // first payload
        _cur_dso_snapshot->first_payload(dso, _dev_inst->get_sample_limit(), sig_enable, _instant);
    } else {
        // Append to the existing data snapshot
        _cur_dso_snapshot->append_payload(dso);
    }
    if (_cur_dso_snapshot->memory_failed()) {
        _error = Malloc_err;
        //session_error();
        cout << "session error!" << endl;
        return;
    }
}*/

//boost::shared_ptr<DsoSnapshot> SigSession::get_snapshot()
//{
//    return _cur_dso_snapshot;
//}

uint16_t SigSession::get_ch_num(int type)
{
    return 2;
}

SigSession::error_state SigSession::get_error() const
{
    return _error;
}

void SigSession::set_error(error_state state)
{
    _error = state;
}

void SigSession::clear_error()
{
    _error_pattern = 0;
    _error = No_err;
}

uint64_t SigSession::get_error_pattern() const
{
    return _error_pattern;
}

SigSession::run_mode SigSession::get_run_mode() const
{
    return _run_mode;
}

void SigSession::set_run_mode(run_mode mode)
{
    _run_mode = mode;
}

int SigSession::get_repeat_intvl() const
{
    return _repeat_intvl;
}

void SigSession::set_repeat_intvl(int interval)
{
    _repeat_intvl = interval;
}

bool SigSession::isRepeating() const
{
    return _repeating;
}
//} // namespace pv
