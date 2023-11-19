#include "tcp_receiver.hh"
// For Lab 2, please replace with a real implementation that passes the
// automated checks run by `make check_lab2`.
using namespace std;
bool TCPReceiver::segment_received(const TCPSegment &seg) {
    static size_t abs_seqno = 0;
    size_t length;
    // 如果连接已建立
    if (SYN_flag == 1) {
        if (seg.header().syn) {
            return false;  // 已建立连接时不应该再收到SYN报文
        }
        // 处理已建立连接的报文
        abs_seqno = unwrap(WrappingInt32(seg.header().seqno.raw_value()), WrappingInt32(_isn), abs_seqno);
        length = seg.length_in_sequence_space();
        if (seg.header().fin) {
            if (FIN_flag == 1) {
                return false;  // 已经收到过FIN报文，不应再收到
            } else {
                FIN_flag = 1;
            }
        } else {
            if (seg.length_in_sequence_space() == 0 && abs_seqno == _base) {
                return true;  // 收到空的报文，直接返回
            } else if (abs_seqno >= _base + window_size() || abs_seqno + length <= _base) {
                return false;  // 报文不在窗口范围内，丢弃
            }
        }
    } else {
        // 本次建立连接
        if (!seg.header().syn) {
            return false;  // 还未建立连接，且收到的不是SYN报文
        }
        SYN_flag = 1;
        _isn = seg.header().seqno.raw_value();
        abs_seqno = 1;
        _base = 1;
        length = seg.length_in_sequence_space() - 1;
        if (length == 0) {
            return true;  // 空的SYN报文，直接返回
        }
    }

    // 将报文的有效部分添加到缓冲区中
    _reassembler.push_substring(seg.payload().copy(), abs_seqno - 1, seg.header().fin);
    _base = _reassembler.head_index() + 1;

    if (_reassembler.input_ended()) {
        _base++;  // FIN被视为一个字节
    }

    return true;
}

optional<WrappingInt32> TCPReceiver::ackno() const {
    if (_base > 0)
        return WrappingInt32(wrap(_base, WrappingInt32(_isn)));
    else
        return std::nullopt;
}

size_t TCPReceiver::window_size() const { return _capacity - _reassembler.stream_out().buffer_size(); }
