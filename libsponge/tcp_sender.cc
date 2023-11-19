#include "tcp_sender.hh"

#include "tcp_config.hh"

#include <random>

// Dummy implementation of a TCP sender

// For Lab 3, please replace with a real implementation that passes the
// automated checks run by `make check_lab3`.

template <typename... Targs>
void DUMMY_CODE(Targs &&.../* unused */) {}

using namespace std;

//! \param[in] capacity the capacity of the outgoing byte stream
//! \param[in] retx_timeout the initial amount of time to wait before retransmitting the oldest outstanding segment
//! \param[in] fixed_isn the Initial Sequence Number to use, if set (otherwise uses a random ISN)
TCPSender::TCPSender(const size_t capacity, const uint16_t retx_timeout, const std::optional<WrappingInt32> fixed_isn)
    : _isn(fixed_isn.value_or(WrappingInt32{random_device()()}))
    , _initial_retransmission_timeout{retx_timeout}
    , _stream(capacity)
    , retransmission_timeout{retx_timeout} {}

uint64_t TCPSender::bytes_in_flight() const { return _bytes_in_flight; }

void TCPSender::fill_window() {
    if (!SYN_flag) {
        TCPSegment seg;
        seg.header().syn = true;
        send_segment(seg);
        SYN_flag = true;
        return;
    }
    // 设置当前发送窗口的大小
    size_t current_window_size = 0;
    if (_window_size == 0)  // 如果接收方的窗口大小为0,则设置发送窗口大小为1,发送片段来判断接收方窗口是否打开
    {
        current_window_size = 1;
    } else
        current_window_size = _window_size;
    size_t remain = current_window_size - (_next_seqno - recv_ackno);  // 发送窗口剩余大小
    while ((remain = current_window_size - (_next_seqno - recv_ackno)) != 0 && !FIN_flag) {
        size_t size = min(TCPConfig::MAX_PAYLOAD_SIZE, remain);  // 设置发送载荷大小
        TCPSegment seg;
        string str = _stream.read(size);  // 从字节流获取size大小的字符串
        seg.payload() = Buffer(std::move(str));
        if (seg.length_in_sequence_space() < current_window_size && _stream.eof()) {
            seg.header().fin = true;
            FIN_flag = true;
        }
        if (seg.length_in_sequence_space() == 0)
            return;
        send_segment(seg);
    }
}

//! \param ackno The remote receiver's ackno (acknowledgment number)
//! \param window_size The remote receiver's advertised window size
//! \returns `false` if the ackno appears invalid (acknowledges something the TCPSender hasn't sent yet)
bool TCPSender::ack_received(const WrappingInt32 ackno, const uint16_t window_size) {
    size_t abs_ackno = unwrap(ackno, _isn, recv_ackno);
    if (abs_ackno > _next_seqno)  // 收到的ack比要期待接收的ack还大则返回false
    {
        return false;
    }
    _window_size = window_size;
    if (abs_ackno <= recv_ackno) {
        return true;
    }
    recv_ackno = abs_ackno;
    // 收到了当前ack,说明当前ack之前的报文已经全部收到了,从发送缓存中弹出
    while (!segmentsToSend.empty()) {
        TCPSegment seg = segmentsToSend.front();
        if (unwrap(seg.header().seqno, _isn, _next_seqno) + seg.length_in_sequence_space() <= abs_ackno) {
            _bytes_in_flight -= seg.length_in_sequence_space();
            segmentsToSend.pop();
        } else
            break;
    }
    fill_window();
    retransmission_timeout = _initial_retransmission_timeout;  // 重置RTO
    consecutive_retransmission = 0;
    return true;
}

//! \param[in] ms_since_last_tick the number of milliseconds since the last call to this method
void TCPSender::tick(const size_t ms_since_last_tick)  // 超时重传
{
    timer += ms_since_last_tick;
    if (timer >= retransmission_timeout && !segmentsToSend.empty())  // 发现超时
    {
        _segments_out.push(segmentsToSend.front());  // 在TCPSender中，加入_segments_out则视为已发送
        retransmission_timeout *= 2;                 // 指数回退
        consecutive_retransmission++;                // 记录连续重传的次数
        timer_running = true;
        timer = 0;  // 重新计时
    }
    if (segmentsToSend.empty())  // 发送完毕
    {
        timer = 0;
        timer_running = false;
    }
}
// 跟踪连续重传的次数
unsigned int TCPSender::consecutive_retransmissions() const { return consecutive_retransmission; }
// 生成长度为0的TCPSegment,并正确设置序列号
void TCPSender::send_empty_segment() {
    TCPSegment seg;
    seg.header().seqno = wrap(_next_seqno, _isn);
    _segments_out.push(seg);
}
/*
void TCPSender::send_empty_segment(WrappingInt32 seqno) {
    TCPSegment seg;
    seg.header().seqno = seqno;
    _segments_out.push(seg);
}*/
void TCPSender::send_segment(TCPSegment seg) {
    seg.header().seqno = wrap(_next_seqno, _isn);
    _next_seqno += seg.length_in_sequence_space();  // 下一个seq为当前seq+发送的字节数
    _bytes_in_flight += seg.length_in_sequence_space();
    _segments_out.push(seg);
    segmentsToSend.push(seg);
    if (!timer_running)  // 启动计时器
    {
        timer = 0;
        timer_running = true;
    }
}
