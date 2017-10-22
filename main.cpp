#include <libsigrok4DSL/libsigrok.h>
#include <stdint.h>
#include <iostream>
#include "devicemanager.h"
#include "sigsession.h"
#include "devinst.h"
#include "device.h"
using namespace std;

int main(int argc, char *argv[])
{
    int ret = 0;
    struct sr_context *sr_ctx = NULL;

    sr_log_loglevel_set(SR_LOG_SPEW);

    // Initialise libsigrok
    if (sr_init(&sr_ctx) != SR_OK) {
        printf("ERROR: libsigrok init failed.");
        return 1;
    }

    BlockingQueue<sr_datafeed_dso> dso_queue;
    DeviceManager _device_manager(sr_ctx);
    SigSession _session(_device_manager, dso_queue);

    // Initialise the main frame
    //pv::MainFrame w(device_manager, open_file);
    // Populate the device list and select the initially selected device
    _session.set_default_device();
    //_session.start_hotplug_proc(error_handler);
    ds_trigger_init();


    const sr_dev_inst *const sdi = _session.get_device()->dev_inst();

    std::cout << _session.get_device()->is_trigger_enabled() << std::endl;

    _session.get_device()->set_ch_enable(1, false);

    std::cout << _session.get_device()->get_channel(1)->enabled << std::endl;

    _session.get_device()->set_sample_rate(1000000);
    _session.get_device()->set_limit_samples(1024);


    GVariant *gvar_opts;
    GVariant *gvar;
    gsize num_opts;
    if ((sr_config_list(sdi->driver, sdi, NULL, SR_CONF_DEVICE_SESSIONS, &gvar_opts) == SR_OK)) {
        const int *const options = (const int32_t *)g_variant_get_fixed_array(
                gvar_opts, &num_opts, sizeof(int32_t));
        for (unsigned int i = 0; i < num_opts; i++) {
            const struct sr_config_info *const info =
                    sr_config_info_get(options[i]);
            gvar = _session.get_device()->get_config(NULL, NULL, info->key);
            std::cout << info->id << ":" << g_variant_get_uint64(gvar) << std::endl;

        }
    }
    //shared_ptr<pv::device::DevInst> selected_device = _session.get_device();
    //_device_manager.add_device(selected_device);
    //_session.init_signals();

    //extern struct ds_trigger *trigger;
    //trigger = (ds_trigger *)g_try_malloc0(sizeof(struct ds_trigger));

    //ds_trigger_init();
    _session.start_capture(false);

    /*
    for(;;) {
        uint64_t  sc = _session.get_snapshot()->get_sample_count();
        const boost::shared_ptr<DsoSnapshot> &snapshot = _session.get_snapshot();
        if(!snapshot->has_data(0))
            continue;

        cout << "sample count=" << sc << endl;
        u_char* data = (u_char *)snapshot->get_data();
        for(int i=0;i<sc/4;i+=4) {
            printf("%08X,",*((uint32_t*)(data+i*4)));
        }
        // formula
        // v = *data;
        // xdiv = current div of voltage (eg. 10mv)
        //(127.5-v) * 10 * vdiv / (1 << 8)
    }*/
    getchar();
    //GMainLoop *main_loop = g_main_loop_new(NULL, FALSE);
    //g_main_loop_run(main_loop);
    _session.stop_capture();

    // Destroy libsigrok
    if (sr_ctx)
        sr_exit(sr_ctx);

    return ret;
}