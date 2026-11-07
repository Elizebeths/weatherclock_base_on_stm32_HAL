#ifndef DEVCIE_NET_H
#define DEVCIE_NET_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 仅返回温度和湿度，以 | 分隔；%25 是占位符中 % 的 URL 编码。 */
static const char request[] =	"GET /Wuhan?format=%25t%7C%25h&m HTTP/1.1\r\n"
								"Host: wttr.in\r\n"
								"User-Agent: curl/8.0\r\n"
								"Connection: close\r\n"
								"\r\n";

typedef enum {
    DEVICE_NET_OK = 0,
    DEVICE_NET_ERROR,       /* 模块返回错误 */
    DEVICE_NET_TIMEOUT,     /* 等待超时 */
    DEVICE_NET_BUSY,        /* 正在执行其他操作 */
    DEVICE_NET_INVALID_ARG,
    DEVICE_NET_IO_ERROR     /* 串口异常 */
} device_net_status;


/**
 * @brief 发送 AT ，测试设备硬件是否联通
 * @note  成功接收到AT OK则返回DEVICE_NET_OK，超时或未接收到OK则返回失败
 */
device_net_status device_net_AT();

/**
 * @brief 发送 AT+CWMODE=1 ，将设备设置为Station模式
 * @note  成功接收到AT OK则返回DEVICE_NET_OK，超时或未接收到OK则返回失败
 */
device_net_status device_net_CWMODE();


/**
 * @brief 发送 AT+CWJAP="ssid","pwd" ，尝试设备连接WIFI
 * @note  成功接收到AT OK则返回DEVICE_NET_OK，超时或未接收到OK则返回失败
 */
device_net_status device_net_CWJAP();

/**
 * @brief 发送 AT+CIPMUX=0 ，设置设备为单连接模式
 * @note  成功接收到AT OK则返回DEVICE_NET_OK，超时或未接收到OK则返回失败
 */
device_net_status device_net_CIPMUX();

/**
 * @brief 发送 AT+CIPMODE=0，设置设备为普通传输模式
 * @note  成功接收到AT OK则返回DEVICE_NET_OK，超时或未接收到OK则返回失败
 */
device_net_status device_net_CIPMODE();

/**
 * @brief PING www.wttr.in，成功时将时延（毫秒）写入 *rtt_ms。
 * @note  例如 device_net_PING(&rtt_ms)；失败时不修改输出值，指针不能为 NULL。
 */
device_net_status device_net_PING(uint32_t *rtt_ms);

device_net_status device_net_CIPSTART(const char *host, int host_length);

device_net_status device_net_CIPSEND(size_t request_length);

device_net_status device_net_REQUEST(void);

/**
 * @brief 在主循环/同一任务中解析最近一次完整 HTTP 响应。
 * @note 无返回值；天气和时间分别校验，解析后须分别检查获取接口的返回码。
 */
void get_request_rcv_buff(void);

/**
 * @brief 读取最近一次解析成功的温湿度；温度单位为摄氏度。
 * @retval DEVICE_NET_OK 成功，写入全部输出参数。
 * @retval DEVICE_NET_INVALID_ARG 任一输出指针为 NULL。
 * @retval DEVICE_NET_ERROR 尚无有效数据，或最近一次请求/解析失败。
 * @note  失败不修改任何输出值；请求成功后先调用 get_request_rcv_buff()。
 *        与请求、解析函数一样，在主循环/同一任务中串行调用。
 */
device_net_status device_net_get_weather(int *temp, uint8_t *humi);

/**
 * @brief 读取最近一次解析成功的北京时间（UTC+8）。
 * @note  返回码、调用顺序和失败时输出不变的约定同 device_net_get_weather()。
 *        此接口返回服务器时间快照，不会自动走时，也不检查快照过期。
 */
device_net_status device_net_get_time(int *year, uint8_t *month, uint8_t *day, uint8_t *hour, uint8_t *min, uint8_t *sec);


#ifdef __cplusplus
}
#endif

#endif /* DEVCIE_NET_H */
