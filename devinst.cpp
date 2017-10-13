/*
 * This file is part of the DSView project.
 * DSView is based on PulseView.
 *
 * Copyright (C) 2014 Joel Holdsworth <joel@airwebreathe.org.uk>
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

#include <cassert>

#include "devinst.h"
#include <execinfo.h>


#include "sigsession.h"

//namespace pv {
//namespace device {

// add by manfeel
static void full_write(int fd, const char *buf, size_t len)
{
        while (len > 0) {
                ssize_t ret = write(fd, buf, len);

                if ((ret == -1) && (errno != EINTR))
                        break;

                buf += (size_t) ret;
                len -= (size_t) ret;
        }
}

static void print_backtrace(void)
{
        static const char start[] = ">> - BACKTRACE ----------\n";
        static const char end[]   = "<< - end ----------------\n";

        void *bt[1024];
        int bt_size;
        char **bt_syms;
        int i;

        bt_size = backtrace(bt, 1024);
        bt_syms = backtrace_symbols(bt, bt_size);
        full_write(STDERR_FILENO, start, strlen(start));
        for (i = 1; i < bt_size; i++) {
                size_t len = strlen(bt_syms[i]);
                full_write(STDERR_FILENO, bt_syms[i], len);
                full_write(STDERR_FILENO, "\n", 1);
        }
        full_write(STDERR_FILENO, end, strlen(end));
    free(bt_syms);
}


DevInst::DevInst() :
    _owner(NULL),
    _usable(true)
{
    _id = malloc(1);
}

DevInst::~DevInst()
{
    assert(_id);
    free(_id);
}

void* DevInst::get_id() const
{
    assert(_id);

    return _id;
}

void DevInst::use(SigSession *owner)
{
	assert(owner);
	assert(!_owner);
	_owner = owner;
}

void DevInst::release()
{
	if (_owner) {
		_owner->release_device(this);
		_owner = NULL;
	}
}

SigSession* DevInst::owner() const
{
	return _owner;
}

GVariant* DevInst::get_config(const sr_channel *ch, const sr_channel_group *group, int key)
{
	GVariant *data = NULL;
	assert(_owner);
	sr_dev_inst *const sdi = dev_inst();
	assert(sdi);
    if (sr_config_get(sdi->driver, sdi, ch, group, key, &data) != SR_OK)
		return NULL;
	return data;
}

bool DevInst::set_config(sr_channel *ch, sr_channel_group *group, int key, GVariant *data)
{
	print_backtrace();
	assert(_owner);
	sr_dev_inst *const sdi = dev_inst();
	assert(sdi);
    if(sr_config_set(sdi, ch, group, key, data) == SR_OK) {
		return true;
	}
	return false;
}

GVariant* DevInst::list_config(const sr_channel_group *group, int key)
{
	GVariant *data = NULL;
	assert(_owner);
	sr_dev_inst *const sdi = dev_inst();
	assert(sdi);
	if (sr_config_list(sdi->driver, sdi, group, key, &data) != SR_OK)
		return NULL;
	return data;
}

void DevInst::enable_probe(const sr_channel *probe, bool enable)
{
	assert(_owner);
	sr_dev_inst *const sdi = dev_inst();
	assert(sdi);
	for (const GSList *p = sdi->channels; p; p = p->next)
		if (probe == p->data) {
			const_cast<sr_channel*>(probe)->enabled = enable;
			return;
		}

	// Probe was not found in the device
	assert(0);
}

uint64_t DevInst::get_sample_limit()
{
	uint64_t sample_limit;
    GVariant* gvar = get_config(NULL, NULL, SR_CONF_LIMIT_SAMPLES);
	if (gvar != NULL) {
		sample_limit = g_variant_get_uint64(gvar);
		g_variant_unref(gvar);
	} else {
		sample_limit = 0U;
	}
	return sample_limit;
}

uint64_t DevInst::get_sample_rate()
{
    uint64_t sample_rate;
    GVariant* gvar = get_config(NULL, NULL, SR_CONF_SAMPLERATE);
    if (gvar != NULL) {
        sample_rate = g_variant_get_uint64(gvar);
        g_variant_unref(gvar);
    } else {
        sample_rate = 0U;
    }
    return sample_rate;
}

uint64_t DevInst::get_time_base()
{
    uint64_t time_base;
    GVariant* gvar = get_config(NULL, NULL, SR_CONF_TIMEBASE);
    if (gvar != NULL) {
        time_base = g_variant_get_uint64(gvar);
        g_variant_unref(gvar);
    } else {
        time_base = 0U;
    }
    return time_base;
}


double DevInst::get_sample_time()
{
    uint64_t sample_rate = get_sample_rate();
    uint64_t sample_limit = get_sample_limit();
    double sample_time;

    if (sample_rate == 0)
        sample_time = 0;
    else
        sample_time = sample_limit * 1.0 / sample_rate;

    return sample_time;
}

GSList* DevInst::get_dev_mode_list()
{
    assert(_owner);
    sr_dev_inst *const sdi = dev_inst();
    assert(sdi);
    return sr_dev_mode_list(sdi);
}

std::string DevInst::name()
{
    sr_dev_inst *const sdi = dev_inst();
    assert(sdi);
    return sdi->driver->name;
}

bool DevInst::is_trigger_enabled() const
{
	return false;
}

void DevInst::start()
{
	if (sr_session_start() != SR_OK)
		throw ("Failed to start session.");
}

void DevInst::run()
{
	sr_session_run();
}

bool DevInst::is_usable() const
{
    return _usable;
}

sr_channel* DevInst::get_channel(int ch_index)
{
    assert(ch_index ==0 || ch_index == 1);

    for (const GSList *l = dev_inst()->channels; l; l = l->next) {
        sr_channel *p = (sr_channel *)l->data;
        assert(p);
        if(ch_index == p->index)
            return p;
    }
    return nullptr;
}

void DevInst::set_ch_enable(int ch_index, bool enable) {

    GVariant *gvar;
    bool cur_enable;

    sr_channel* ch=get_channel(ch_index);
    assert(ch);

    gvar = get_config(ch, NULL, SR_CONF_EN_CH);
    if (gvar != NULL) {
        cur_enable = g_variant_get_boolean(gvar);
        g_variant_unref(gvar);
    } else {
        std::cout << "ERROR: config_get SR_CONF_EN_CH failed." << std::endl;
        return;
    }
    if (cur_enable == enable)
        return;

    set_config(ch, NULL, SR_CONF_EN_CH, g_variant_new_boolean(enable));
}

void DevInst::set_sample_rate(uint64_t sample_rate)
{
    set_config(NULL, NULL,
               SR_CONF_SAMPLERATE,
               g_variant_new_uint64(sample_rate));
}

void DevInst::set_limit_samples(uint64_t sample_count)
{
    set_config(NULL, NULL,
               SR_CONF_LIMIT_SAMPLES,
               g_variant_new_uint64(sample_count));
}
//} // device
//} // pv
