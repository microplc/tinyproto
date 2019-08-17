/*
    Copyright 2019 (C) Alexey Dynda

    This file is part of Tiny Protocol Library.

    Protocol Library is free software: you can redistribute it and/or modify
    it under the terms of the GNU Lesser General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    Protocol Library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU Lesser General Public License for more details.

    You should have received a copy of the GNU Lesser General Public License
    along with Protocol Library.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "tiny_hdlc_helper.h"

TinyHdlcHelper::TinyHdlcHelper(FakeChannel * channel,
                               const std::function<void(uint8_t*,int)> &onRxFrameCb,
                               const std::function<void(uint8_t*,int)> &onTxFrameCb,
                               int rx_buf_size)
    : IBaseHelper( channel, rx_buf_size )
    , m_handle{}
    , m_onRxFrameCb(onRxFrameCb)
    , m_onTxFrameCb(onTxFrameCb)
{
    m_handle.user_data = this;
    m_handle.send_tx = write_data;
    m_handle.on_frame_read = onRxFrame;
    m_handle.on_frame_sent = onTxFrame;
    m_handle.rx_buf = malloc( rx_buf_size );
    m_handle.rx_buf_size = rx_buf_size;
    hdlc_init( &m_handle );
}

int TinyHdlcHelper::send(uint8_t *buf, int len)
{
    int result = hdlc_send( &m_handle, (uint8_t *)buf, len, 1000);
    return result;
}


int TinyHdlcHelper::run()
{
    uint8_t byte;
    while ( m_channel->read(&byte, 1) == 1 )
    {
        int res, error;
        do
        {
            res = hdlc_run_rx( &m_handle, &byte, 1, &error );
        } while (res == 0 && error == 0);
        if (error < 0)
        {
            return error;
        }
    }
    return 0;
}

int TinyHdlcHelper::onRxFrame(void *handle, void * buf, int len)
{
    TinyHdlcHelper * helper = reinterpret_cast<TinyHdlcHelper *>( handle );
    if (helper->m_onRxFrameCb)
    {
        helper->m_onRxFrameCb((uint8_t *)buf, len);
    }
    return 0;
}

int TinyHdlcHelper::onTxFrame(void *handle, const void * buf, int len)
{
    TinyHdlcHelper * helper = reinterpret_cast<TinyHdlcHelper *>( handle );
    if (helper->m_onTxFrameCb)
    {
        helper->m_onTxFrameCb((uint8_t *)buf, len);
    }
    return 0;
}

TinyHdlcHelper::~TinyHdlcHelper()
{
    hdlc_close( &m_handle );
    free( m_handle.rx_buf );
}

