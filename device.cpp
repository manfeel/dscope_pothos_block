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

#include <sstream>
#include <iostream>
#include "device.h"

using std::ostringstream;
using std::string;


namespace pv {
namespace device {

Device::Device(sr_dev_inst *sdi) :
    _sdi(sdi)
{
	assert(_sdi);
}

sr_dev_inst* Device::dev_inst() const
{
	return _sdi;
}

void Device::use(SigSession *owner)
{
    DevInst::use(owner);
    std::cout << "in func (sr_session_new):" << __func__ << std::endl;
    sr_session_new();

    assert(_sdi);
    sr_dev_open(_sdi);
    _usable = (_sdi->status == SR_ST_ACTIVE);
    if (sr_session_dev_add(_sdi) != SR_OK)
        throw ("Failed to use device.");
}

void Device::release()
{
	if (_owner) {
		DevInst::release();
		sr_session_destroy();
	}

	sr_dev_close(_sdi);
}

string Device::format_device_title() const
{
	ostringstream s;

	assert(_sdi);

	if (_sdi->vendor && _sdi->vendor[0]) {
		s << _sdi->vendor;
		if ((_sdi->model && _sdi->model[0]) ||
			(_sdi->version && _sdi->version[0]))
			s << ' ';
	}

	if (_sdi->model && _sdi->model[0]) {
		s << _sdi->model;
		if (_sdi->version && _sdi->version[0])
			s << ' ';
	}

	if (_sdi->version && _sdi->version[0])
		s << _sdi->version;

    return s.str();
}

bool Device::is_trigger_enabled() const
{
	assert(_sdi);
	for (const GSList *l = _sdi->channels; l; l = l->next) {
		const sr_channel *const p = (const sr_channel *)l->data;
		assert(p);
		if (p->trigger && p->trigger[0] != '\0')
			return true;
	}
	return false;
}

} // device
} // pv
