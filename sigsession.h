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

#ifndef DSVIEW_PV_SIGSESSION_H
#define DSVIEW_PV_SIGSESSION_H

#include <boost/function.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/weak_ptr.hpp>
#include <boost/thread.hpp>

#include <string>
#include <utility>
#include <map>
#include <set>
#include <string>
#include <vector>
#include <stdint.h>

#include <libsigrok4DSL/libsigrok.h>
#include <libusb.h>

struct srd_decoder;
struct srd_channel;

namespace pv {

class DeviceManager;

namespace data {
class SignalData;
class Snapshot;
class Analog;
class AnalogSnapshot;
class Dso;
class DsoSnapshot;
class Logic;
class LogicSnapshot;
class Group;
class GroupSnapshot;
class DecoderModel;
}

namespace device {
class DevInst;
}

namespace view {
class Signal;
class GroupSignal;
class DecodeTrace;
class MathTrace;
}

namespace decoder {
class Decoder;
class DecoderFactory;
}

class SigSession
{

private:
    static constexpr float Oversampling = 2.0f;
    static const int RefreshTime = 500;
    static const int RepeatHoldDiv = 20;

public:
    static const int ViewTime = 50;
    static const int WaitShowTime = 500;

public:
	enum capture_state {
        Init,
		Stopped,
		Running
	};

    enum run_mode {
        Single,
        Repetitive
    };

    enum error_state {
        No_err,
        Hw_err,
        Malloc_err,
        Test_data_err,
        Test_timeout_err,
        Pkt_data_err,
        Data_overflow
    };

public:
	SigSession(DeviceManager &device_manager);

	~SigSession();

    boost::shared_ptr<device::DevInst> get_device() const;

	/**
	 * Sets device instance that will be used in the next capture session.
	 */
    void set_device(boost::shared_ptr<device::DevInst> dev_inst);

    void set_default_device();

    void release_device(device::DevInst *dev_inst);

	capture_state get_capture_state() const;

    uint64_t cur_samplerate() const;
    uint64_t cur_samplelimits() const;
    double cur_sampletime() const;
    void set_cur_samplerate(uint64_t samplerate);
    void set_cur_samplelimits(uint64_t samplelimits);

    void start_capture(bool instant);
	void stop_capture();
    void capture_init();
    bool get_capture_status(bool &triggered, int &progress);

    uint16_t get_ch_num(int type);
    
    bool get_instant();

    error_state get_error() const;
    void set_error(error_state state);
    void clear_error();
    uint64_t get_error_pattern() const;

    run_mode get_run_mode() const;
    void set_run_mode(run_mode mode);
    int get_repeat_intvl() const;
    void set_repeat_intvl(int interval);
    bool isRepeating() const;

private:
	void set_capture_state(capture_state state);

private:
    void sample_thread_proc(boost::shared_ptr<device::DevInst> dev_inst);

	void data_feed_in(const struct sr_dev_inst *sdi,
		const struct sr_datafeed_packet *packet);
	static void data_feed_in_proc(const struct sr_dev_inst *sdi,
		const struct sr_datafeed_packet *packet, void *cb_data);

private:
	DeviceManager &_device_manager;

	/**
	 * The device instance that will be used in the next capture session.
	 */
    boost::shared_ptr<device::DevInst> _dev_inst;

    mutable boost::mutex _sampling_mutex;
	capture_state _capture_state;
    bool _instant;
    uint64_t _cur_samplerate;
    uint64_t _cur_samplelimits;

    int _group_cnt;

	std::unique_ptr<boost::thread> _sampling_thread;

	libusb_hotplug_callback_handle _hotplug_handle;
    std::unique_ptr<boost::thread> _hotplug;
    bool _hot_attach;
    bool _hot_detach;

    int    _noData_cnt;
    bool _data_lock;
    bool _data_updated;

    uint64_t _trigger_pos;
    bool _trigger_flag;
    bool _hw_replied;

    error_state _error;
    uint64_t _error_pattern;

    run_mode _run_mode;
    int _repeat_intvl;
    bool _repeating;
    int _repeat_hold_prg;

    int _map_zoom;

private:
	// TODO: This should not be necessary. Multiple concurrent
	// sessions should should be supported and it should be
	// possible to associate a pointer with a ds_session.
	static SigSession *_session;
};

} // namespace pv

#endif // DSVIEW_PV_SIGSESSION_H
