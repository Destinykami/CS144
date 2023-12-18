#include "tcp_connection.hh"

#include <iostream>
// For Lab 4, please replace with a real implementation that passes the
// automated checks run by `make check`.
using namespace std;
// 发送窗口剩余空间
size_t TCPConnection::remaining_outbound_capacity() const { return _sender.stream_in().remaining_capacity(); }
// 已发送，但是还未收到ack的字节数
size_t TCPConnection::bytes_in_flight() const { return _sender.bytes_in_flight(); }
// 未按序到达的字节数
size_t TCPConnection::unassembled_bytes() const { return _receiver.unassembled_bytes(); }
// 上一次收到报文经过的时间
size_t TCPConnection::time_since_last_segment_received() const { return _time_since_last_segment_received; }
// 当前TCP是否还active
bool TCPConnection::active() const { return _active; }
// 接收TCP数据报
void TCPConnection::segment_received(const TCPSegment &seg) {
    if (!_active)
        return;
    _time_since_last_segment_received = 0;
    //收到RST直接异常终止
    if (seg.header().rst) {
        unclean_shutdown(false);
        return;
    }
    //SYN_SENT状态不能收到带有数据载荷的ack
    if (TCPState::state_summary(_sender) == TCPSenderStateSummary::SYN_SENT 
        && seg.header().ack && seg.payload().size() > 0) 
    {
        return;
    }
    bool send_empty = false;
    //收到ACK包，更新sender的状态
    if (_sender.next_seqno_absolute() > 0 && seg.header().ack) {
        if (!_sender.ack_received(seg.header().ackno, seg.header().win)) {
            send_empty = true;
        }
    }
    //接收方接收报文
    bool recv_flag = _receiver.segment_received(seg);
    if (!recv_flag) {
        send_empty = true;
    }
    // 接收器初次接收到SYN包,并且此时发送器还处于关闭状态,所以当前由LISTEN转为了SYN_SENT状态
    if (TCPState::state_summary(_receiver) == TCPReceiverStateSummary::SYN_RECV &&
        TCPState::state_summary(_sender) == TCPSenderStateSummary::CLOSED) {
        connect();
        return;
    }
    if (seg.length_in_sequence_space() > 0) {
        send_empty = true;
    }
    //需要keep-alive
    if (send_empty && TCPState::state_summary(_receiver) != TCPReceiverStateSummary::LISTEN) {
        _sender.send_empty_segment();
    }
    send_segments();
}

size_t TCPConnection::write(const string &data) {
    size_t write_size = _sender.stream_in().write(data);  // 向写入缓冲写入数据
    send_segments();
    return write_size;
}

//! \param[in] ms_since_last_tick number of milliseconds since the last call to this method
void TCPConnection::tick(const size_t ms_since_last_tick) {
    if (!_active)
        return;
    _time_since_last_segment_received += ms_since_last_tick;
    _sender.tick(ms_since_last_tick);
    // 重传次数太多时需要断开连接
    if (_sender.consecutive_retransmissions() > TCPConfig::MAX_RETX_ATTEMPTS) {
        unclean_shutdown(true);
    }
    send_segments();
}

void TCPConnection::end_input_stream() {
    // 关闭发送端的写入通道
    _sender.stream_in().end_input();
    send_segments();
}

void TCPConnection::connect() {
    // 建立连接
    send_segments(true);
}

TCPConnection::~TCPConnection() {
    try {
        // 如果TCP连接是活跃状态则关闭连接
        if (active()) {
            cerr << "Warning: Unclean shutdown of TCPConnection\n";
            unclean_shutdown(true);
        }
    } catch (const exception &e) {
        std::cerr << "Exception destructing TCP FSM: " << e.what() << std::endl;
    }
}
// 将待发送的数据包加上期望接受到数据的ackno和当前自己作为接收端的滑动窗口大小
bool TCPConnection::send_segments(bool send_syn) {
    _sender.fill_window(send_syn || TCPState::state_summary(_receiver) == TCPReceiverStateSummary::SYN_RECV);
    TCPSegment seg;
    while (!_sender.segments_out().empty()) {
        // 从发送端的传输队列取出要发送的TCP数据报
        seg = _sender.segments_out().front();
        _sender.segments_out().pop();
        if (_receiver.ackno().has_value()) {  // 判断当前是否处于LISTEN状态
            // 向接收方发送ACK/下一个期望收到的ackno/当前接收窗口大小
            seg.header().ack = true;
            seg.header().ackno = _receiver.ackno().value();
            seg.header().win = _receiver.window_size();
        }
        // 出现异常设置RST位
        if (_need_send_rst) {
            _need_send_rst = false;
            seg.header().rst = true;
        }
        _segments_out.push(seg);
    }
    clean_shutdown();
    return true;
}
// 出现异常结束连接
void TCPConnection::unclean_shutdown(bool send_rst) {
    _receiver.stream_out().set_error();
    _sender.stream_in().set_error();
    _active = false;
    if (send_rst) {
        _need_send_rst = true;
        if (_sender.segments_out().empty()) {
            _sender.send_empty_segment();
        }
        send_segments();
    }
}
// 正常结束连接
bool TCPConnection::clean_shutdown() {
    if (_receiver.stream_out().input_ended() && !(_sender.stream_in().eof())) {
        _linger_after_streams_finish = false;
    }
    if (TCPState::state_summary(_receiver) == TCPReceiverStateSummary::FIN_RECV &&
        TCPState::state_summary(_sender) == TCPSenderStateSummary::FIN_ACKED) {
        if (!_linger_after_streams_finish || time_since_last_segment_received() >= 10 * _cfg.rt_timeout) {
            // 等待2MSL
            _active = false;
        }
    }
    return !_active;
}
