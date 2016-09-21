分布式uuid基础模块
版本：uuid-1.0

使用前，参写XCC环境的使用： [wiki] (https://github.com/xcodecraft/home/wiki)

一，简介：
采用了高性能的服务器框架，源于Redis系统的网络IO部分代码，具有业界公认的高性能和高稳定性。
在单CPU虚拟机上进行简单测试，单实例每秒可以响应10000次以上的请求。
理论上每个机房部署两台服务器即可满足整个机房所有应用所有UUID的需求。

采用了精密的算法，保证了UUID在“秒”级别能够保持顺序递增等特性:防被攻击（抓全量数据，数据量估计）等。

每个实例保持独立，实例间无任何通信过程，可以方便的进行七层、四层、客户端等形式的负载均衡。

具有良好的可扩展性，最多可以部署16个实例。

提供rest json接口以及memcache get协议通用接口

二，原理：
uuid为52bit的整型: all(52bit)=time(28bit)+instance(6bit)+seq(18bit)
time:Linux时间戳- 1251414000
instance：实例号（0~15， 只使用4bit保留了2bit）
seq：自增序列号（最大支持每秒25w）

三，服务端使用说明：
可执行程序：bin/uuid-server
参数选项：
uuid server version 1.0
-p <num>      TCP port number to listen on (default: 5001)
-l <ip_addr>  interface to listen on (default: INADDR_ANY, all addresses)
-d            run as a daemon
-c <num>      max simultaneous connections (default: 1024)
-v            verbose (print errors/warnings while in event loop)
-i <num>      the instance id, must be a number between 0 and 15
必须指定的参数：
-p 端口号
-d 以daemon在后台运行
-i 系统全局idc唯一的标识，必须在0-15之间，绝对不能重复。
ha实例号
运行示例：
src/uuid-server -p 5001 -d -i 0
建议将多个实例的启动命令行写为脚本程序，防止发生错误。

四，客户端使用说明：
使用Memcache客户端直接连uuid-server，调用get(key)即可获得uuid。
特殊说明：
1，key支持忽略头尾空格，但是不支持忽略和其他特殊字符；
例如：“get  1uuid321 ”；
2，错误处理和以前uuid错误处理一样，切断连接。

五，测试方法：
使用任何支持文本协议的Memcache客户端或者telnet均可测试。
请求key为biz+uuid+randomId。
示例：
wumings-MacBook-Pro:uuid yangwm$ telnet 127.0.0.1 5001
Trying 127.0.0.1...
Connected to 127.0.0.1.
Escape character is '^]'.
get uuid321
VALUE uuid 0 16
2282689923122176
END
性能测试：
$ php -c ./conf/used/php.ini ./test/core/uuid_benchmark.php
0, time:1387538380, uuid:2283788130566144
100000, time:1387538385, uuid:2283788215320576
200000, time:1387538390, uuid:2283788300075008
300000, time:1387538395, uuid:2283788384829440
400000, time:1387538400, uuid:2283788469583872
500000, time:1387538409, uuid:2283788621447168
600000, time:1387538414, uuid:2283788706201600
700000, time:1387538420, uuid:2283788807733248
800000, time:1387538424, uuid:2283788875710464
900000, time:1387538429, uuid:2283788960464896
1000000, time:1387538434, uuid:2283789045219328
string(16) "2283789045219328"
