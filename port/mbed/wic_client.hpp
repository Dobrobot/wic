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
#include "wic_input_pool.hpp"
#include "wic_buffer.hpp"
#include "wic_user_queue.hpp"

namespace WIC {

    class ClientBase {

        protected:

            enum State {

                CLOSED,
                OPENING,
                OPEN,
                CLOSING

            } state;

            NetworkInterface &interface;

            UserQueueBase& user_queue;            
            InputPoolBase& rx_pool;
            OutputQueueBase& tx_queue;
            BufferBase& url;

            /* these are the buffers that wic_inst directly
             * interacts with */
            BufferBase *rx;
            BufferBase *tx;

            Semaphore work;
            Ticker ticker;

            TCPSocket tcp;
            TLSSocketWrapper tls;
            Socket &socket;
            
            Mutex mutex;
            ConditionVariable condition;
            EventQueue events;
            
            static const uint32_t max_redirects = 3U;

            struct Job {

                bool done;
                nsapi_error_t retval;
                enum wic_status status;
                enum wic_handshake_failure handshake_failure_reason;
            };

            int timeout_id;
            
            Job job;

            Thread worker_thread;

            struct wic_inst inst;

            Callback<void()> on_open_cb;
            Callback<void(uint16_t, const char *, uint16_t)> on_close_cb;

            static ClientBase *to_obj(struct wic_inst *self);

            static void handle_send(struct wic_inst *self, const void *data, size_t size, enum wic_buffer type);
            static void *handle_buffer(struct wic_inst *self, size_t size, enum wic_buffer type, size_t *max);
            static uint32_t handle_rand(struct wic_inst *self);
            static void handle_handshake_failure(struct wic_inst *self, enum wic_handshake_failure reason);
            static void handle_open(struct wic_inst *self);
            static bool handle_message(struct wic_inst *self, enum wic_encoding encoding, bool fin, const char *data, uint16_t size);
            static void handle_close(struct wic_inst *self, uint16_t code, const char *reason, uint16_t size);
            static void handle_close_transport(struct wic_inst *self);
            static void handle_ping(struct wic_inst *self);
            static void handle_pong(struct wic_inst *self);            

            /* these are requested via public methods */
            void do_open();
            void do_close();
            void do_send(enum wic_encoding encoding, bool fin, const char *value, uint16_t size);
            void do_close_socket();

            void do_handshake_timeout();
            void do_work();

            void do_tick()
            {
                do_work();
            }

            void do_sigio()
            {
                do_work();
            }

            void worker_task();

            void notify()
            {
                mutex.lock();
                condition.notify_one();
                mutex.unlock();                
            }

            void wait()
            {
                condition.wait();
            }

        public:

            ClientBase(NetworkInterface &interface, UserQueueBase& user_queue, InputPoolBase& rx_pool, OutputQueueBase& tx_queue, BufferBase& url);

            /** Open the websocket
             *
             * @param[in] url
             *
             * @return nsapi_error_t
             *
             * open will return one of the following codes:
             * 
             * @retval NSAPI_ERROR_OK                   success
             * @retval NSAPI_ERROR_PARAMETER            badly formatted URL
             * @retval NSAPI_ERROR_IS_CONNECTED         already open
             * @retval NSAPI_ERROR_CONNECTION_LOST      socket closed unexpectedly
             * @retval NSAPI_ERROR_CONNECTION_TIMEOUT   socket timeout
             *
             * */
            nsapi_error_t open(const char *url);

            /* close an open websocket */
            void close();

            //bool set_header(String key, String value);
            //bool get_header(String key, String &value);
            
            //bool enable_ping(uint32_t interval, uint32_t response_time);
            //bool disable_ping();

            
            /** receive a websocket frame
             *
             * @param[out] encoding     indicates the encoding of the data (binary or UTF8)
             * @param[out] fin          true if final fragment
             * @param[in/out] buffer    user supplied buffer
             * @param[in] timeout       by default this interface will block until a message is received or the socket closes
             *
             * The memory buffer must be at least the size of the largest
             * possible payload. This will be known by the application
             * since the Client is parametrised with it.
             *
             * @return nsapi_size_or_error_t
             *
             * @retval >0                           size of message received
             * @retval 0                            websocket was closed
             * @retval NSAPI_ERROR_WOULD_BLOCK      
             * @retval NSAPI_ERROR_NO_SOCKET        not open
             * 
             * */
            nsapi_size_or_error_t recv(enum wic_encoding& encoding, bool &fin, char *buffer, uint32_t timeout = osWaitForever);

            /** send a message
             *
             * @param[in] encoding      specify the encoding (default is UTF8)
             * @param[in] fin           true if final fragment (default is true)
             * @param[in] data
             * @param[in] size
             *
             * @return nsapi_size_or_error_t
             *
             * @retval >= 0 bytes of data send
             *
             * @retval NSAPI_ERROR_WOULD_BLOCK      
             * @retval NSAPI_ERROR_NO_SOCKET        not open
             * @retval NSAPI_ERROR_PARAMETER        
             *
             * */
            nsapi_size_or_error_t send(const char *data, uint16_t size, enum wic_encoding encoding = WIC_ENCODING_UTF8, bool fin = true);

            /* is websocket open? */
            bool is_open();

            void on_open(Callback<void()> handler);
            void on_close(Callback<void(uint16_t, const char *, uint16_t)> handler);

            /* TLS settings */
            nsapi_error_t set_root_ca_cert(const void *root_ca, size_t len);
            nsapi_error_t set_root_ca_cert(const char *root_ca_pem);
            nsapi_error_t set_client_cert_key(const char *client_cert_pem, const char *client_private_key_pem);
            nsapi_error_t set_client_cert_key(const void *client_cert_pem, size_t client_cert_len, const void *client_private_key_pem, size_t client_private_key_len);            
    };

    template<size_t RX_MAX, size_t TX_MAX, size_t URL_MAX = 200>
    class Client : public ClientBase {

        protected:

            UserQueue<RX_MAX, 2U> _user_queue;
            OutputQueue<TX_MAX> _tx_queue;
            InputPool<RX_MAX> _rx_pool;
            Buffer<URL_MAX> _url;
            
        public:

            Client(NetworkInterface &interface) :
                ClientBase(interface, _user_queue, _rx_pool, _tx_queue, _url)
            {};
    };
};

#endif
