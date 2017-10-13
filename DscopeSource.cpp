// Copyright (c) 2014-2016 Josh Blum
// SPDX-License-Identifier: BSL-1.0

#include <libsigrok4DSL/libsigrok.h>
#include <algorithm> //min/max
#include <iostream>
#include <Pothos/Framework.hpp>
#include <Poco/Logger.h>
#include <chrono>

#include "devicemanager.h"
#include "sigsession.h"

using namespace std;
//char DS_RES_PATH[256];//="/usr/local/share/DSView/res/";

/***********************************************************************
 * |PothosDoc DSCope(Virtual Oscilloscope)
 *
 * The audio source forwards an audio input device to an output sample stream.
 * In interleaved mode, the samples are interleaved into one output port,
 * In the port-per-channel mode, each audio channel uses a separate port.
 *
 * The audio source will post a sample rate stream label named "rxRate"
 * on the first call to work() after activate() has been called.
 * Downstream blocks like the plotter widgets can consume this label
 * and use it to set internal parameters like the axis scaling.
 *
 * |category /DreamSourceLab
 * |category /Sources
 * |keywords dscope oscilloscope
 *
 *
 * |param sampRate[Sample Rate] The rate of audio samples.
 * |option 1e6
 * |option 2e6
 * |option 4e6
 * |default 1e6
 * |units Sps
 * |widget ComboBox(editable=true)
 *
 * |param logLvl[Debug Log Level]
 * |option [SR_LOG_NONE] 0
 * |option [SR_LOG_ERR]  1
 * |option [SR_LOG_WARN] 2
 * |option [SR_LOG_INFO] 3
 * |option [SR_LOG_DBG]  4
 * |option [SR_LOG_SPEW] 5
 * |default 1
 *
 * |param dtype[Data Type] The data type produced by the audio source.
 * |option [Float32] "float32"
 * |option [Int32] "int32"
 * |option [Int16] "int16"
 * |option [Int8] "int8"
 * |option [UInt8] "uint8"
 * |default "float32"
 * |preview disable
 *
 * |factory /dsl/dscope(dtype)
 * |initializer setupDevice(logLvl)
 **********************************************************************/
class DscopeSource : public Pothos::Block {
protected:
    //bool _interleaved;
    bool _sendLabel;
    bool _reportLogger;
    bool _reportStderror;
    std::chrono::high_resolution_clock::duration _backoffTime;
    std::chrono::high_resolution_clock::time_point _readyTime;
    struct sr_context *sr_ctx = NULL;
    DeviceManager *_device_manager = NULL;
    SigSession *_session = NULL;

public:
    DscopeSource(const Pothos::DType &dtype)
    {

        this->registerCall(this, POTHOS_FCN_TUPLE(DscopeSource, setupDevice));

        this->setupOutput(0, dtype);
        this->setupOutput(1, dtype);
        //cout << __func__ << endl;
    }

    static Pothos::Block *make(const Pothos::DType &dtype) {
        cout << __func__ << dtype.name() << endl;
        return (Pothos::Block*)new DscopeSource(dtype);
    }

    void setupDevice(int logLvl) {
        cout << __func__ << endl;
        sr_log_loglevel_set(logLvl);

        // Initialise libsigrok
        if (sr_init(&sr_ctx) != SR_OK) {
            throw Pothos::Exception(__func__, "ERROR: libsigrok init failed.");
        }

        try {
            _device_manager = new DeviceManager(sr_ctx);
            _session = new SigSession(*_device_manager);

            _session->set_default_device();
            //_session.start_hotplug_proc(error_handler);
            ds_trigger_init();
        } catch(Pothos::Exception e) {
            std::cout << e.message() << std::endl;
        }
    }

    void activate(void) {
        _session->start_capture(false);
    }

    void deactivate(void) {
        _session->stop_capture();
    }

    void work(void) {
        if (this->workInfo().minOutElements == 0) return;

        //calculate the number of frames
        int numFrames = 1024; //Pa_GetStreamReadAvailable(_stream);
        if (numFrames <
            0) { ;//throw Pothos::Exception("DscopeSource::work()", "Pa_GetStreamReadAvailable: " + std::string(Pa_GetErrorText(numFrames)));
        }
        if (numFrames == 0) numFrames = MIN_FRAMES_BLOCKING;
        numFrames = std::min<int>(numFrames, this->workInfo().minOutElements);

        //get the buffer
        void *buffer = (void *) this->workInfo().outputPointers.data();

        //peform read from the device
        /*
            PaError err = Pa_ReadStream(_stream, buffer, numFrames);

            //handle the error reporting
            bool logError = err != paNoError;
            if (err == paInputOverflowed)
            {
                _readyTime += _backoffTime;
                if (_reportStderror) std::cerr << "aO" << std::flush;
                logError = _reportLogger;
            }
            if (logError)
            {
                poco_error(_logger, "Pa_ReadStream: " + std::string(Pa_GetErrorText(err)));
            }
        */
        if (_sendLabel) {
            _sendLabel = false;
            const auto rate = 1024;//Pa_GetStreamInfo(_stream)->sampleRate;
            Pothos::Label label("rxRate", rate, 0);
            for (auto port : this->outputs()) port->postLabel(label);
        }

        //not ready to produce because of backoff
        if (_readyTime >= std::chrono::high_resolution_clock::now()) return this->yield();

        //produce buffer (all modes)
        for (auto port : this->outputs()) port->produce(numFrames);
    }
};

static Pothos::BlockRegistry registerDscope("/dsl/dscope", &DscopeSource::make);
