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

#ifndef WIC_CLIENT_HPP
#define WIC_CLIENT_HPP

#include "mbed.h"
#include "wic.h"
#include "TLSSocket.h"
#include "wic_output_queue.hpp"
#include "wic_input_queue.hpp"
#include "wic_buffer.hpp"

namespace WIC {

    class ClientBase {

        protected:

            enum State {

                CLOSED,
                OPEN

            } state;

            static const uint32_t socket_open_flag = 1U;

            NetworkInterface &interface;

            BufferBase& rx;            
            InputQueueBase& input;
            OutputQueueBase& output;
            
            TCPSocket tcp;
            TLSSocketWrapper tls;
            Socket &socket;            
            Mutex mutex;
            Semaphore wakeup;
            EventQueue events;
            EventFlags flags;

            struct Job {

                bool done;
                enum wic_handshake_failure failure;
                enum wic_status status;
            };

            int timeout_id;
            BufferBase *tx;
            Job job;
            
            Thread writer_thread;        
            Thread reader_thread;
            Thread event_thread;

            struct wic_inst inst;

            enum wic_schema schema;

            Callback<void(bool,const char *, uint16_t)> on_text_cb;
            Callback<void(bool,const void *, uint16_t)> on_binary_cb;
            Callback<void()> on_open_cb;
            Callback<void(uint16_t, const char *, uint16_t)> on_close_cb;

            static ClientBase *to_obj(struct wic_inst *self);
            static void handle_send(struct wic_inst *self, const void *data, size_t size, enum wic_frame_type type);
            static void *handle_buffer(struct wic_inst *self, size_t size, enum wic_frame_type type, size_t *max);
            static uint32_t handle_rand(struct wic_inst *self);
            static void handle_handshake_failure(struct wic_inst *self, enum wic_handshake_failure reason);
            static void handle_open(struct wic_inst *self);
            static void handle_text(struct wic_inst *self, bool fin, const char *data, uint16_t size);
            static void handle_binary(struct wic_inst *self, bool fin, const void *data, uint16_t size);
            static void handle_close(struct wic_inst *self, uint16_t code, const char *reason, uint16_t size);
            static void handle_close_transport(struct wic_inst *self);

            /* these are requested via public methods */
            void do_open(const char *url);
            void do_close();
            void do_send_text(bool fin, const char *value, uint16_t size);
            void do_send_binary(bool fin, const void *value, uint16_t size);

            void do_parse();            
            void do_transport_error();
            void do_handshake_timeout();

            void writer_task();
            void reader_task();

        public:

            ClientBase(NetworkInterface &interface, BufferBase &rx, InputQueueBase &input, OutputQueueBase &output);

            enum wic_status open(const char *url);

            void close();

            enum wic_status text(const char *value);
            enum wic_status text(const char *value, uint16_t size);
            enum wic_status text(bool fin, const char *value);
            enum wic_status text(bool fin, const char *value, uint16_t size);
            
            enum wic_status binary(const void *value, uint16_t size);
            enum wic_status binary(bool fin, const void *value, uint16_t size);
            
            bool is_open();
            
            void on_text(Callback<void(bool,const char *, uint16_t)> handler);            
            void on_binary(Callback<void(bool,const void *, uint16_t)> handler);
            void on_open(Callback<void()> handler);
            void on_close(Callback<void(uint16_t, const char *, uint16_t)> handler);
            
            nsapi_error_t set_root_ca_cert(const void *root_ca, size_t len);
            nsapi_error_t set_root_ca_cert(const char *root_ca_pem);
            nsapi_error_t set_client_cert_key(const char *client_cert_pem, const char *client_private_key_pem);
            nsapi_error_t set_client_cert_key(const void *client_cert_pem, size_t client_cert_len, const void *client_private_key_pem, size_t client_private_key_len);            
    };

    template<size_t RX_MAX, size_t TX_MAX>
    class Client : public ClientBase {

        protected:

            Buffer<RX_MAX> _rx;
            OutputQueue<TX_MAX> _output;
            InputQueue<RX_MAX> _input;

        public:

            Client(NetworkInterface &interface) :
                ClientBase(interface, _rx, _input, _output)
            {
            };

    };
};

#endif
