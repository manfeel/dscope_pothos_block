// Copyright (c) 2017 Manfeel
// manfeel@foxmail.com

#include <libsigrok4DSL/libsigrok.h>
#include <algorithm> //min/max
#include <iostream>
#include <Pothos/Framework.hpp>
#include <Poco/Logger.h>
#include <chrono>
#include <random>
#include "devicemanager.h"
#include "sigsession.h"
#include "blockingqueue.hpp"

using namespace std;
//char DS_RES_PATH[256];//="/usr/local/share/DSView/res/";

/***********************************************************************
 * |PothosDoc DSCope(Virtual Oscilloscope)
 *
 * The dscope source capture the waveform to an output sample stream.
 *
 * The dscope source will post a sample rate stream label named "rxRate"
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
 * |option [1M] 1e6
 * |option [2M] 2e6
 * |option [5M] 5e6
 * |option [10M] 10e6
 * |option [20M] 20e6
 * |option [50M] 50e6
 * |option [100M] 100e6
 * |option [200M] 200e6
 * |default 100e6
 * |units Sps
 * |widget ComboBox(editable=true)
 *
 * |param vdiv[Voltage Div] The rate of voltage.
 * |option [10mv] 10
 * |option [20mv] 20
 * |option [50mv] 50
 * |option [100mv] 100
 * |option [200mv] 200
 * |option [500mv] 500
 * |option [1v] 1000
 * |option [2v] 2000
 * |default 50
 * |units mv
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
 * |default "float32"
 * |preview disable
 *
 * |factory /dsl/dscope(dtype)
 * |initializer setupDevice(dtype)
 * |setter setSamplerate(sampRate)
 * |setter setVdiv(vdiv)
 * |setter setLogLevel(logLvl)
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
    BlockingQueue<sr_datafeed_dso> *dso_queue = NULL;

public:
    DscopeSource(const Pothos::DType &dtype)
    {

        this->registerCall(this, POTHOS_FCN_TUPLE(DscopeSource, setupDevice));
        this->registerCall(this, POTHOS_FCN_TUPLE(DscopeSource, setSamplerate));
        this->registerCall(this, POTHOS_FCN_TUPLE(DscopeSource, setVdiv));
        this->registerCall(this, POTHOS_FCN_TUPLE(DscopeSource, setLogLevel));

        this->setupOutput(0, dtype);
        //this->setupOutput(1, dtype);

        // Initialise libsigrok for the first time
        if (sr_init(&sr_ctx) != SR_OK) {
            throw Pothos::Exception(__func__, "ERROR: libsigrok init failed for the first time!");
        }
    }

    static Pothos::Block *make(const Pothos::DType &dtype) {
        return (Pothos::Block*)new DscopeSource(dtype);
    }

    void setVdiv(uint64_t vdiv) {
        _session->get_device()->set_voltage_div(0, vdiv);
    }

    void setSamplerate(uint64_t samplerate) {
        _session->get_device()->set_sample_rate(samplerate);
    }

    void setLogLevel(int logLvl) {
        cout<< __func__ << "(" << logLvl << ") has been called." << endl;
        sr_log_loglevel_set(logLvl);
    }

    // the param just a dummy for initializer( must have params! why?!)
    void setupDevice(const Pothos::DType &dtype) {
        // Initialise libsigrok for the second time!
        if(dtype.name() != "float32") {
            throw Pothos::Exception(__func__, "Error: dscope ONLY accept float32 data type!");
        }

        if (sr_init(&sr_ctx) != SR_OK) {
            throw Pothos::Exception(__func__, "ERROR: libsigrok init failed for the second time!");
        }

        try {
            dso_queue = new BlockingQueue<sr_datafeed_dso>();
            _device_manager = new DeviceManager(sr_ctx);
            _session = new SigSession(*_device_manager, *dso_queue);

            _session->set_default_device();
            ds_trigger_init();

            _session->register_hotplug_callback();
            _session->start_hotplug_proc();

            //_session->get_device()->set_ch_enable(1, false);
            //_session->get_device()->set_limit_samples(2048);

        } catch(Pothos::Exception e) {
            std::cout << e.message() << std::endl;
            throw e;
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

        int  numFrames = _session->get_device()->get_sample_limit();
        numFrames = std::min<int>(numFrames, this->workInfo().minOutElements);
        auto outPort0 = this->output(0);
        auto buffer = outPort0->buffer().as<float*>();
        const size_t numElems = outPort0->elements();
        uint64_t vdiv = _session->get_device()->get_voltage_div(0);
        sr_datafeed_dso dso = dso_queue->take();

        for(uint64_t i=0;i<numElems;i++) {
            uint8_t b = ((uint8_t *)dso.data)[i];
            //buffer[i]=(127.5 - b) * 10 * vdiv / 256.0f;
            buffer[i]=(127.5 - b) * vdiv / 25.6f;
        }

        if (_sendLabel) {
            _sendLabel = false;
            const auto rate = _session->get_device()->get_sample_rate();
            Pothos::Label label("rxRate", rate, 0);
            for (auto port : this->outputs()) port->postLabel(label);
        }

        //not ready to produce because of backoff
        //if (_readyTime >= std::chrono::high_resolution_clock::now()) return this->yield();

        //produce buffer (all modes)
        outPort0->produce(numElems);
    }
};

static Pothos::BlockRegistry registerDscope("/dsl/dscope", &DscopeSource::make);
