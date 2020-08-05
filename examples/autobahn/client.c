/* Copyright (c) 2020 Cameron Harper
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
 * the Software, and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 * 
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * */

#include "wic.h"
#include "transport.h"
#include "log.h"

#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <time.h>

static void on_close(struct wic_inst *inst, uint16_t code, const char *reason, uint16_t size);
static void on_text(struct wic_inst *inst, bool fin, const char *data, uint16_t size);
static void on_binary(struct wic_inst *inst, bool fin, const void *data, uint16_t size);
static void on_open(struct wic_inst *inst);
static void on_send(struct wic_inst *inst, const void *data, size_t size, enum wic_frame_type type);
static uint32_t do_random(struct wic_inst *inst);
static void on_text_case_count(struct wic_inst *inst, bool fin, const char *data, uint16_t size);
static void on_close_transport(struct wic_inst *inst);
static void *on_buffer(struct wic_inst *inst, size_t min_size, enum wic_frame_type type, size_t *max_size);

static int n = 0;

static void do_client(int *s, struct wic_inst *inst, const struct wic_init_arg *arg)
{
    if(!wic_init(inst, arg)){

        ERROR("could not init instance")
        exit(EXIT_FAILURE);
    }

    if(
        transport_open_client(
            wic_get_url_schema(inst),
            wic_get_url_hostname(inst),
            wic_get_url_port(inst),
            s
        )
    ){
        wic_start(inst);

        while(transport_recv(*s, inst));

        wic_close(inst);
    }
}

int main(int argc, char **argv)
{
    static struct wic_inst inst;
    static uint8_t rx_buffer[UINT16_MAX];
    static char url[1000U];
    
    int tc;

    srand(time(NULL));

    static int s = -1;

    struct wic_init_arg arg = {0};

    arg.rx = rx_buffer;
    arg.rx_max = sizeof(rx_buffer);
    arg.on_open = on_open;    
    arg.on_close = on_close;
    arg.on_send = on_send;
    arg.on_buffer = on_buffer;
    arg.on_close_transport = on_close_transport;
    arg.rand = do_random;
    arg.app = &s;
    arg.role = WIC_ROLE_CLIENT;
    arg.url = url;

    arg.url = "ws://localhost:9001/getCaseCount?agent=wic";
    arg.on_text = on_text_case_count;
    
    do_client(&s, &inst, &arg);

    arg.url = url;
    arg.on_text = on_text;
    arg.on_binary = on_binary;
    
    for(tc=1; tc <= n; tc++){

        snprintf(url, sizeof(url), "ws://localhost:9001/runCase?case=%d&agent=wic", tc);

        LOG("test #%d...", tc)
        
        do_client(&s, &inst, &arg);
    }

    arg.url = "ws://localhost:9001/updateReports?agent=wic";
    arg.on_text = NULL;
    arg.on_binary = NULL;

    do_client(&s, &inst, &arg);

    LOG("exiting...")

    exit(EXIT_SUCCESS);
}

static void on_close(struct wic_inst *inst, uint16_t code, const char *reason, uint16_t size)
{
    transport_close((int *)wic_get_app(inst));    
}

static void on_text(struct wic_inst *inst, bool fin, const char *data, uint16_t size)
{
    wic_send_text(inst, fin, data, size);
}

static void on_text_case_count(struct wic_inst *inst, bool fin, const char *data, uint16_t size)
{
    if(size > 0){

        n = atoi(data);
    }
    
    wic_close(inst);
}

static void on_binary(struct wic_inst *inst, bool fin, const void *data, uint16_t size)
{
    wic_send_binary(inst, fin, data, size);
}

static void on_open(struct wic_inst *inst)
{
}

static void on_send(struct wic_inst *inst, const void *data, size_t size, enum wic_frame_type type)
{
    if(!transport_write(*(int *)wic_get_app(inst), data, size)){

        ERROR("transport_write()")

        wic_close_with_reason(inst, WIC_CLOSE_ABNORMAL_1, NULL, 0U);
    }
}

static void on_close_transport(struct wic_inst *inst)
{
    transport_close((int *)wic_get_app(inst));
}

static uint32_t do_random(struct wic_inst *inst)
{
    return rand();
}

static void *on_buffer(struct wic_inst *inst, size_t min_size, enum wic_frame_type type, size_t *max_size)
{
    static uint8_t tx[UINT16_MAX+100UL];

    *max_size = sizeof(tx);

    return tx;
}
