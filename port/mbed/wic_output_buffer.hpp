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

#include "mbed.h"

namespace WIC {

    class BufferBase {

        public:

            const uint32_t mask;
            const uint32_t priority;
            
            size_t size;
            size_t max;
            uint8_t *data;

            BufferBase(uint32_t mask, uint32_t priority)
                : mask(mask), priority(priority)
            {}
    };

    template<size_t MAX_SIZE>
    class Buffer : public BufferBase {
        
        protected:

            uint8_t _data[MAX_SIZE];
            
        public:

            Buffer()
            {
                data = _data;
                max = sizeof(_data);
            }

            Buffer(uint32_t mask, uint32_t priority)
                : BufferBase(mask, priority)
            {
                data = _data;
                max = sizeof(_data);
            }
            
            bool init(const void *data, size_t size)
            {
                bool retval = false;
                
                if(size <= sizeof(_data)){

                    (void)memcpy(_data, data, size);
                    this->size = size;

                    retval = true;
                }
                
                return retval;
            }
    };
};

