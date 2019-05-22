//#include <boost/coroutine2/coroutine.hpp>
#include "generator.hpp"
#include "scheduler.hpp"
#include <cstdint>
#include <functional>
#include <iomanip>
#include <iostream>
#include <optional>
#include <random>
#include <thread>
#include <vector>

using namespace std::literals;

// 論理的なワード(受け取った情報に応じて次の送信ワードを変更することができる最小単位。処理系の処理能力に依存する。)
// ここではuint32_t(32bit)を1ワードとする。
// 2つのPortから同時に送信した場合、bitwise-orされた結果を受信する、と定義された通信路を考えている。
using Data = std::uint32_t;

// 乱数
namespace Global
{
std::mt19937 random{std::random_device{}()};
}

// 通信の統計を取るための構造体
struct RecordEntry {
    double send_time;  // 送信が成功した時刻
    double wait_time;  // キューに貯められてから送信が成功するまでに待った時間
    int data_size;     // 送信したサイズ(word単位)
    int sender;        // 送信したAgentのid
    friend std::ostream& operator<<(std::ostream& os, const RecordEntry& record)
    {
        return os << "t: " << record.send_time << " id: " << record.sender << " w: " << record.wait_time << " s: " << record.data_size;
    }
};
std::vector<RecordEntry> records;
// バグチェックのために送信数と受信数を記録して、同じになっていることを確認する。
int sent_num = 0;
int received_num = 0;


struct Port;

// 通信路を定義するクラス。
struct Bus {

    // Portの参照を持つ
    std::vector<std::reference_wrapper<Port>> ports;
    // 通信路の長さ
    double length;

    // 時刻t、場所xで受信されるはずのデータを取得する
    Data data(double x, double t) const;
    // 時刻Global::t、場所xで受信されるデータを取得する
    Data data(double x) const { return data(x, Global::t); }

    // バス上の点からもう一つの点に行くまでにかかる時間を計算する。
    // 注意: 新規手法では、測定器を送信機の上流につけるため、自分の出したデータは一周戻ってきて初めて測定可能になる。(それを使って衝突判定を行う)
    double distance(double from, double to) const
    {
        return to > from ? to - from : length + to - from;
    }
};

// 送信データの情報を表す構造体
struct Entry {
    double t;
    Data data;
};
// 送受信機器を表す構造体
struct Port {
    Bus& bus;                         // 通信路の参照を持っておく
    double x;                         // 通信路上の位置
    std::vector<Entry> history = {};  //送信した履歴
    void send(Data data)
    {
        // 送信処理は、履歴に新しいEntryを追加する。
        // Bus::data(x, t)は各Portの履歴から最新のものを探し、bitwise-orを行って受信データを計算する。
        // 送信したwordは別のwordを新たに送信しない限りずっと同じ情報が出続けている状態になる。
        // 送信をやめたい時は0(最弱の信号)を送信すること。
        history.push_back({Global::t, data});
    }
    Data receive() const
    {
        // 受信処理は、busから現在の位置で受け取れるはずのデータを読む。
        return bus.data(x);
    }
};

// 時刻t、場所xで受信されるはずのデータを取得する
Data Bus::data(double x, double t) const
{

    Data data = 0;
    for (const Port& port : ports) {
        for (int i = int(port.history.size()) - 1; i >= 0; i--) {
            if (port.history[i].t <= t - distance(port.x, x)) {
                data |= port.history[i].data;
                break;
            }
        }
    }
    //    std::cout << "data(" << x << ", " << t << ") = " << data << '\n';
    return data;
}


// メッセージを表す構造体。本体と、送信元・送信先の情報がある。
struct Message {
    int from;
    int to;
    std::vector<Data> data;
    friend std::ostream& operator<<(std::ostream& os, const Message& msg)
    {
        os << "[" << msg.from << "->" << msg.to << "]";
        int i = 10;
        for (Data d : msg.data) {
            os << " " << d;
            i--;
            if (i == 0) {
                os << " ...";
                break;
            }
        }
        return os;
    }
};

// 通信する人を表す。
struct Agent {
    std::reference_wrapper<Port> port;
    int id;
    int packet_data_length = 10;  // パケットサイズ

    double bus_length = 100;                                         // 通信路長
    int agent_num = 10;                                              // 人数
    std::function<void(const Message&)> receive_callback = nullptr;  //受信した時に呼ばれる処理


    double dt = 1.0;          // 時間刻み幅
    Data start_frame = 0x95;  // 送信始めを表すワード
    Data end_frame = 0xba;    // 送信終わりを表すワード(本来はいらないが、CSMA/CDと比較するためにこれも送っている)

    std::queue<std::pair<double, Message>> buffer{};  // 送信キュー


    // ログを出力するためのstd::ostreamを取得する。
    std::ostream& log() const
    {
        return std::clog << std::fixed << std::setprecision(5) << Global::t << ": " << id << "> ";
    }
    // 通信路からIDを一つ受信する。受信し終わったら、最後の1wordを読める状態で読んだIDをco_returnする。
    future<int> readId()
    {
        // Data bit0 = receive();
        // co_await dt;
        // Data bit1 = receive();
        co_return((int)port.get().receive());
    }

    // 通信路にIDを一つ送信する。送信し終わるまで、1wordずつco_yieldする。
    generator<Data> writeId(int id)
    {
        co_yield((Data)id);
    }
    //
    //    template<class F>
    //    int readLength(F&& stream){
    //        return (int)stream();
    //    }


    // 通信路にメッセージを一つ送信する。送信し終わるまで、1wordずつco_yieldする。
    generator<Data> sendCoroutine(const Message& msg)
    {
        log() << "Now sending: " << msg << '\n';
        //        co_yield start_frame;
        for (Data data : writeId(msg.from)) {
            co_yield data;
        }
        for (Data data : writeId(msg.to)) {
            co_yield data;
        }
        for (int i = 0; i < packet_data_length; i++) {
            if (i < int(msg.data.size())) {
                co_yield msg.data[i];
            } else {
                co_yield 0;
            }
        }
        co_yield end_frame;
    }

    // メッセージを受信する。受信したらコールバックを呼んで、その後co_returnする。
    // 衝突しないので、素直な実装になっている。
    future<void> receiveMessage()
    {

        //        log() << "Receive start" << '\n';
        Message msg;
        co_await dt;
        msg.from = co_await readId();
        //        log() << "From: " << msg.from << '\n';
        co_await dt;
        msg.to = co_await readId();
        //        log() << "To: " << msg.to << '\n';
        msg.data.resize(packet_data_length);
        for (int i = 0; i < packet_data_length; i++) {
            co_await dt;
            msg.data[i] = port.get().receive();
            //            log() << "Received byte: " << msg.data[i] << '\n';
        }
        co_await dt;
        Data last_data = port.get().receive();
        if (last_data == end_frame) {
            //            log() << "Received: " << msg << '\n';
            if (msg.to == id) {
                received_num++;
                if (receive_callback) {
                    receive_callback(msg);
                }
            }
        } else {
            log() << "Error! expected " << end_frame << ", but got " << last_data << '\n';
        }
    }

    // 接続を始める。ここに新規手法のアルゴリズムの本体がある。
    future<void> start()
    {
        co_await port.get().x;  // スタート時のオフセット(通信路の遅延にピッタリと合わせる)


        while (true) {
            // "Sync Point"

            //            log() << "sync\n";
            bool wants_to_send = !buffer.empty();  // 送信キューに溜まっているかどうか
            // 送信キューに溜まっているなら「俺が送る」word、溜まっていないなら0を送る
            if (wants_to_send) {
                port.get().send(start_frame);
            } else {
                port.get().send(0);
            }
            co_await(dt * 0.5);  // 受信は信号が安定するのを待つ


            Data recv_first = port.get().receive();  // 自分の出した信号は一周回ってこない限り受信しないことに注意
            if (recv_first == start_frame) {         // 自分の上流から「俺が送る」wordが流れてきたら
                //だれか自分より優先度が高い人が話そうとしているので黙る
                port.get().send(0);
                co_await receiveMessage();
                co_await(dt * 0.5);        // 送る方に合わせる
            } else if (recv_first == 0) {  // 自分の上流から「俺が送る」wordが流れて来なかったら
                if (wants_to_send) {
                    co_await(dt * 0.5);  // 送信キューに溜まっていれば、次から即送り始める

                    for (Data data : sendCoroutine(buffer.front().second)) {
                        //                        log() << "sending data " << data << '\n';
                        port.get().send(data);
                        co_await dt;
                    }
                    port.get().send(0);

                    sent_num++;

                    records.push_back({Global::t, Global::t - buffer.front().first, packet_data_length, this->id});

                    log() << "sent: " << buffer.front().second << ", wait_time: " << Global::t - buffer.front().first << '\n';
                    buffer.pop();
                    co_await bus_length;  //連続で2パケット送らない。

                } else {  // 送信キューに溜まっていないし、誰もデータを送らない場合
                    port.get().send(0);
                    co_await(bus_length - dt * 0.5);  //次のSync Pointまで待つ
                }
            } else {
                log() << "Error! expected 0 or start_frame, but received " << recv_first << '\n';
                std::abort();
            }
        }
    }

    // 送信キューに貯める
    void send(Message&& msg)
    {
        msg.from = id;
        log() << "queueing message " << msg << '\n';
        buffer.push({Global::t, std::move(msg)});
    }
};


int main(int argc, char** argv)
{
    std::ios_base::sync_with_stdio(false);
    Bus bus;  // バスの長さは100word分

#if 1

    bus.length = 100;         // バスの長さは100word分
    int packet_length = 101;  // パケットサイズ(バスの長さより長い必要がある)
    int n = argc - 1;         // コマンドライン入力にメッセージ生成確率が人数分入ってくる
    std::vector<double> rates;
    for (int i = 0; i < n; i++) {
        rates.push_back(std::atof(argv[i]));  // メッセージ生成確率
    }

    std::vector<Port> ports;
    std::vector<Agent> agents;
    ports.reserve(n);
    agents.reserve(n);

    std::vector<double> positions;  // 各送受信機の位置は乱数で生成し、順番に並べ替える
    positions.reserve(n);
    for (int i = 0; i < n; i++) {
        positions.push_back(std::uniform_real_distribution<double>{0, bus.length}(Global::random));
    }

    std::sort(positions.begin(), positions.end());

    for (int i = 0; i < n; i++) {
        ports.push_back({bus, positions[i]});
        bus.ports.push_back(std::ref(ports.back()));
        agents.push_back({std::ref(ports.back()), i, packet_length, bus.length, n, [&agents, i](const Message& msg) {
                              agents[i].log() << "Received: " << msg << ", sent_num: " << sent_num << ", received_num: " << received_num << '\n';
                          }});
        std::cout << "Agent " << i << ": " << positions[i] << std::endl;
    }

    double T = 10000000;


    std::vector<future<void>> futures;
    futures.reserve(n);

    // メッセージ生成イベントを事前に全部スケジュールしておく。メッセージは生成確率で定義される指数分布で生成する。
    for (int i = 0; i < n; i++) {
        futures.push_back(agents[i].start());
        //        double t_start = std::uniform_real_distribution{0.0, 1.0}(Global::random);
        //        Global::queue.push({t_start, [&agents, &futures, i] { futures.push_back(agents[i].start()); }});

        double t = 0;
        while (t <= T) {
            double dt = std::exponential_distribution{rates[i] / (packet_length + 4)}(Global::random);
            t += dt;
            std::vector<Data> data;
            data.reserve(/*agents[i].packet_data_length*/ 5);
            for (int j = 0; j < /*agents[i].packet_data_length*/ 5; j++) {
                data.push_back(i + 1);
            }

            Global::queue.push({t, [&agents, i, n, data = std::move(data)]() mutable {
                                    int send_to = std::uniform_int_distribution{0, n - 2}(Global::random);
                                    agents[i].send({i,
                                        send_to >= i ? send_to + 1 : send_to,  //自分自身には送らない
                                        std::move(data)});
                                }});
        }
    }

#else
    // テスト用コード

    bus.length = 20;
    int n = 3;
    int packet_length = 6;

    Port port_a{bus, 0.0};
    Port port_b{bus, 16.2};
    Port port_c{bus, 4.1};
    bus.ports.push_back(std::ref(port_a));
    bus.ports.push_back(std::ref(port_b));
    bus.ports.push_back(std::ref(port_c));

    Agent agent_a{port_a, 0, packet_length, bus.length, n, [&](const Message& msg) {
                      agent_a.log() << "Received Message: " << msg << '\n';
                  }};

    Agent agent_b{port_b, 1, packet_length, bus.length, n, [&](const Message& msg) {
                      agent_b.log() << "Received Message: " << msg << '\n';
                  }};

    auto future_a = agent_a.start();
    auto future_b = agent_b.start();

    Global::queue.push({1.6, [&] { agent_a.send({0, 1, {16, 32, 1}}); }});
    Global::queue.push({35.6, [&] { agent_b.send({1, 0, {26, 3, 21}}); }});
    Global::queue.push({55.6, [&] { agent_a.send({0, 1, {100, 200, 300, 400, 500, 600}}); }});
    Global::queue.push({57.6, [&] { agent_a.send({0, 2, {100, 200, 300, 400, 500, 600}}); }});
    Global::queue.push({60.2, [&] { agent_b.send({1, 0, {11, 22, 33, 44, 55, 66}}); }});

    constexpr double T = 1000;
#endif


    while (!Global::queue.empty()) {
        const auto& schedule = Global::queue.top();
        Global::t = schedule.time;

        //        std::cout << "Scheduler: " << Global::t << '\n';
        if (Global::t > T)
            break;

        schedule.task();
        Global::queue.pop();
        //        std::this_thread::sleep_for(0.1s);
    }


    // 結果の表示
    std::cout << "============================" << '\n';
    // 送信成功ログ
    std::cout << "Records: " << '\n';
    for (const auto& r : records) {
        std::cout << r << '\n';
    }
    std::cout << "============================" << '\n';
    std::cout << "Statistics: " << '\n';

    double wait_time_sum = 0;
    double wait_time_max = 0;
    int sent_data_sum = 0;
    int record_n = 0;

    std::vector<double> wait_time_list;
    for (const auto& r : records) {
        wait_time_sum += r.wait_time;
        wait_time_max = std::max(wait_time_max, r.wait_time);
        sent_data_sum += r.data_size;
        record_n += 1;
        wait_time_list.push_back(r.wait_time);
    }
    std::sort(wait_time_list.begin(), wait_time_list.end());


    //平均遅延時間(キューに入ってから送信されるまでの時間)
    std::cout << "Average wait time: " << wait_time_sum / record_n << '\n';

    for (int i = 0; i <= 10; i++) {
        // 遅延時間の分布の下位10i%の値
        std::cout << "Wait time " << i * 10 << "%: " << wait_time_list.at((wait_time_list.size() - 1) * i / 10) << '\n';
    }
    std::cout << "Average efficiency: " << sent_data_sum / T << '\n';  // 通信路効率
    std::cout << "record_n: " << record_n << std::endl;                // 送信成功数
    std::cout << "received_num: " << received_num << std::endl;        // 受信成功数
    return 0;
}
