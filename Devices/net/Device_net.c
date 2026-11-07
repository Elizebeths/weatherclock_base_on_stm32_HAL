#include "net/Device_net.h"
#include "common/port.h"
#include "bus/uart_port.h"
#include <string.h>
#include <stdio.h>

#define LOG_TAG "Device_net"
#define LOG_LVL ELOG_LVL_INFO
#include "elog.h"
/* DMA 缓冲区必须长期有效 */
static uint8_t s_dma_buf[128];

/* AT 响应可能分多次进入接收回调，需要另行拼接 */
static char s_response[512];
static uint16_t s_response_len;

static bool s_wait_prompt = false;

static uint8_t s_dma_buf_request[1024];
//static uint8_t request_weather[8];// 解析天气
static int request_temp;				// 解析温度（可能为负数）
static uint8_t request_humi;			// 解析湿度
static bool request_weather_valid;
static bool request_time_valid;
static bool s_http_response_valid;

/* HTTP Date 字段原文（GMT），保留到下一次解析；空串表示未获取到。 */
static char request_time[64];		// 解析出的完整字符串
static int request_year;
static uint8_t request_month;
static uint8_t request_day;
static uint8_t request_hour;
static uint8_t request_min;
static uint8_t request_sec;


typedef enum {
    AT_IDLE = 0,
    AT_WAITING,
    AT_OK,
    AT_FAIL,
	AT_REQUEST
} at_state_t;

static volatile at_state_t s_at_state = AT_IDLE;

/* PING 的额外结果：0ms 也是有效时延，所以另设一个“是否收到”的标记。 */
static volatile uint32_t s_ping_ms;
static volatile bool s_ping_received;

/* 中断中只拼接、解析响应行；即使 O、K、CR、LF 分批到达也能识别。 */
static void device_net_at_rx(const uint8_t *data, uint16_t len, void *arg)
{
    (void)arg;

    /* 一次回调只处理本批字节；未收完的响应行留给下一次回调继续拼接。 */
    for (uint16_t i = 0U; (i < len) && (s_at_state == AT_WAITING); ++i)
    {
    	if (s_wait_prompt && data[i] == '>')
    	{
    	    s_at_state = AT_REQUEST;
    	    return;
    	}
        if (data[i] == '\n') /* 收到换行后，检查前面拼接出的完整响应行。 */
        {
            /* 去掉行尾 CR，按整行匹配，避免把 SEND OK 等内容当作成功。*/
            if ((s_response_len > 0U) && (s_response[s_response_len - 1U] == '\r'))
            {
                --s_response_len;
            }
            // 拼接结束符
            s_response[s_response_len] = '\0';

            if (strcmp(s_response, "OK") == 0)	// 单独的一行OK，则成功
            {
            	if (!s_wait_prompt)
            	{
            	    s_at_state = AT_OK;
            	}
            }
            else if (strcmp(s_response, "SEND OK") == 0)	// 单独的一行SEND OK，则成功
			{
				s_at_state = AT_OK;				// 改变信号量
			}
            else if ((strcmp(s_response, "ERROR") == 0) || (strcmp(s_response, "FAIL") == 0))
            {
                s_at_state = AT_FAIL;
            }
            else if (strcmp(s_response, ">") == 0 )
            {
				s_at_state = AT_REQUEST;
				return;
			}
            else
            {
                /* PING 中间结果形式可能是 +PING:时延，也可能是旧固件的 +时延。 */
                const char *number = NULL;// 检测是否为时延数据的指针
                // 对比s_response的前6字节，是否为 +PING:
                if (strncmp(s_response, "+PING:", 6U) == 0) 	number = s_response + 6;
                else if (s_response[0] == '+') 					number = s_response + 1;

                // 若无时延头，则number将保持NULL，单纯有数值不会被认为是时延
                if ((number != NULL) && (*number != '\0'))// 此时指针指向非空或非结束，则说明还
                {
                    uint32_t value = 0U;
                    bool valid = true;
                    for (; *number != '\0'; ++number)
                    {
                    	// 该值小于0、大于9、或者延迟大于1000
                        if ((*number < '0') || (*number > '9') || (value > 1000))
                        {
                            valid = false;
                            break;
                        }
                        value = value * 10U + (uint32_t)(*number - '0');
                    }
                    if (valid)
                    {
                        s_ping_ms = value;
                        s_ping_received = true;
                    }
                }
            }
            /* 当前行处理完了，清空长度，准备接收下一行。 */
            s_response_len = 0U;
        }
        else
        {
            if (s_response_len >= sizeof(s_response) - 1U) 	// 回复数组的长度已经超过接收数组的长度了，接收失败
            {
                s_at_state = AT_FAIL;
                return;
            }
            s_response[s_response_len++] = (char)data[i];	// 将data填入回复数组
        }
    }
}


/**
 * @brief 公共 AT 执行流程：启动接收、发送命令、等待最终响应并清理。
 * @param command 		以 '\0' 结尾且包含 "\r\n" 的完整 AT 命令。
 * @param timeout_ms 	发送与等待响应的总超时，单位 ms，必须大于 0。
 * @note  仅支持以完整 OK 行表示成功的命令；接收由 device_net_at_rx 处理。
 */
static device_net_status device_net_at_exec(const char *command, uint32_t timeout_ms)
{
    device_net_status result = DEVICE_NET_ERROR;

    // 1.合法性检验
    //1.2检查参数是否合理
    if ((command == NULL) || (timeout_ms == 0U))
    {
        return DEVICE_NET_INVALID_ARG;
    }
    const size_t command_len = strlen(command);
    //1.2检查指令的长度是否合法，是不是\r\n结尾
    if ((command_len < 2U) || (command_len > UINT16_MAX) || (command[command_len - 2U] != '\r') || (command[command_len - 1U] != '\n'))
    {
        return DEVICE_NET_INVALID_ARG;
    }

    /* 在主循环/任务中调用，需保持串口中断与毫秒时基正常运行。 */
    port_enter_critical();			// 进入临界区了，关闭中断
    if (s_at_state != AT_IDLE)		// s_at_state为临界资源，信号量，表达了当前指令的执行状态
    {
        port_exit_critical();		// 退出临界区了，打开中断
        return DEVICE_NET_BUSY;
    }
    s_at_state = AT_WAITING;		// 将信号量置为等待接收
    port_exit_critical();			// 退出临界区了，打开中断，两次退出一定只有一个被执行了

    /* 不抢占其他代码已经启动的接收。 */
    if (uart_port_init() != PORT_OK)
    {
        s_at_state = AT_IDLE;
        return DEVICE_NET_IO_ERROR;
    }
    if (uart_port_rx_is_active())
    {
        s_at_state = AT_IDLE;
        return DEVICE_NET_BUSY;
    }

    // 为接收的缓冲区准备好地方，该缓冲区用于拼接回复
    s_http_response_valid = false;
    s_response_len = 0U;
    s_response[0] = '\0';
    s_ping_ms = 0U;
    s_ping_received = false;//标记接收结果是否有效，防止出现0ms被误判为无效值

    /* 先启动DMA接收，将接收到的数据放入s_dma_buf，并且保存接收回调函数
     * 启动接收再发送，避免漏掉模块快速返回的 OK，此时不代表已经接收完毕了。 */
    if (uart_port_rx_start(s_dma_buf, sizeof(s_dma_buf), device_net_at_rx, NULL) != PORT_OK)
    {
        s_at_state = AT_IDLE;
        log_e("AT 响应接收启动失败");
        return DEVICE_NET_IO_ERROR;
    }

    /* 发送和等待共用一个总超时；单次串口发送最多等 8000ms。 */
    const uint32_t started_ms = port_time_ms();
    const uint32_t tx_timeout_ms = (timeout_ms < 100U) ? timeout_ms : 8000U;

    //正式发送，并保存发送结果
    const port_status_t tx_status = uart_port_write(UART_PORT_CH_NET, (const uint8_t *)command, (uint16_t)command_len, tx_timeout_ms);
    if (tx_status == PORT_OK)
    {
        /* 主循环在这里等待，中断回调收到 OK/ERROR 后更新 s_at_state。 */
        while ((s_at_state == AT_WAITING) && ((uint32_t)(port_time_ms() - started_ms) < timeout_ms))
            port_delay_ms(1U);

        if ((uint32_t)(port_time_ms() - started_ms) >= timeout_ms)
        {
        	log_e("等待 AT 响应超时");
            result = DEVICE_NET_TIMEOUT;
        }
        else if ((s_at_state == AT_OK) || (s_at_state == AT_REQUEST))
            result = DEVICE_NET_OK;
        else
            result = DEVICE_NET_ERROR;
    }
    else
    {
    	// 失败时判段发送失败原因并返回
        result = (tx_status == PORT_TIMEOUT) ? DEVICE_NET_TIMEOUT : DEVICE_NET_IO_ERROR;
        (tx_status == PORT_TIMEOUT) ? log_e("发送超时") : log_e("发送IO出错");
    }

    /* 无论成功、错误还是超时，都结束本次接收，允许下次重新测试。 */
    const port_status_t rx_status = uart_port_rx_stop();
    if (rx_status != PORT_OK)
    {
    	log_e("无法结束接收");
        result = DEVICE_NET_IO_ERROR;
    }

    if (s_at_state != AT_REQUEST)
        s_at_state = AT_IDLE;
    return result;
}

/**
 * @brief 发送 AT ，测试设备硬件是否联通
 * @note  成功接收到AT OK则返回DEVICE_NET_OK，超时或未接收到OK则返回失败
 */
device_net_status device_net_AT()
{
    return device_net_at_exec("AT\r\n", 300U);
}

/**
 * @brief 发送 AT+CWMODE=1 ，将设备设置为Station模式
 * @note  成功接收到AT OK则返回DEVICE_NET_OK，超时或未接收到OK则返回失败
 */
device_net_status device_net_CWMODE()
{
	return device_net_at_exec("AT+CWMODE=1\r\n", 300U);
}


/**
 * @brief 发送 AT+CWJAP="ssid","pwd" ，尝试设备连接WIFI
 * @note  成功接收到AT OK则返回DEVICE_NET_OK，超时或未接收到OK则返回失败
 */
device_net_status device_net_CWJAP()
{
	return device_net_at_exec("AT+CWJAP=\"www\",\"www123456\"\r\n", 8000);
}

/**
 * @brief 发送 AT+CIPMUX=0 ，设置设备为连接模式
 * @note  成功接收到AT OK则返回DEVICE_NET_OK，超时或未接收到OK则返回失败
 */
device_net_status device_net_CIPMUX()
{
	return device_net_at_exec("AT+CIPMUX=0\r\n", 300U);
}

/**
 * @brief 发送 AT+CIPMODE=0 1，设置设备为普通传输模式
 * @note  成功接收到AT OK则返回DEVICE_NET_OK，超时或未接收到OK则返回失败
 */
device_net_status device_net_CIPMODE()
{
	return device_net_at_exec("AT+CIPMODE=0\r\n", 300U);
}

/**
 * @brief PING 成功时，通过输出参数返回时延；失败时不修改调用者的值。
 */
device_net_status device_net_PING(uint32_t *rtt_ms)
{
    if (rtt_ms == NULL)
        return DEVICE_NET_INVALID_ARG;

    device_net_status result = device_net_at_exec("AT+PING=\"www.wttr.in\"\r\n", 1000U);
    if (result != DEVICE_NET_OK)
        return result;
    if (!s_ping_received) /* 只有 OK、没有时延，不算完整的 PING 结果。 */
        return DEVICE_NET_ERROR;

    *rtt_ms = s_ping_ms;
    return DEVICE_NET_OK;
}

device_net_status device_net_CIPSTART(const char *host, int host_length)
{
	log_d("开始建立 TCP 连接");
//  "AT+CIPSTART=\"TCP\",\"wttr.in\",80\r\n",
    const char prefix[] = "AT+CIPSTART=\"TCP\",\"";
    const char suffix[] = "\",80\r\n";
    char command[128];

    const size_t prefix_len = sizeof(prefix) - 1U;


    // 从此开始处理异常情况
    if ((host == NULL) || (host_length <= 0))
    {
    	log_e("TCP 主机地址为空或长度无效");
        return DEVICE_NET_INVALID_ARG;
    }

    const size_t host_len = (size_t)host_length;

    /* suffix 的 sizeof 包含最终的 '\0'。 */
    if (host_len > sizeof(command) - prefix_len - sizeof(suffix))
    {
    	log_e("TCP 主机地址超过命令缓冲区容量");
        return DEVICE_NET_INVALID_ARG;
    }

    /* host_length 不包含 '\0'；拒绝破坏命令格式的字符。 */
    for (size_t i = 0U; i < host_len; ++i)
    {
        if ((host[i] == '\0') || (host[i] == '"') || (host[i] == '\\') || (host[i] == '\r') || (host[i] == '\n'))
        {
        	log_e("TCP 主机地址包含非法字符");
            return DEVICE_NET_INVALID_ARG;
        }
    }
    // 在此之前均为异常情况处理

    // 字符串拼接
    memcpy(command, prefix, prefix_len);
    memcpy(command + prefix_len, host, host_len);
    memcpy(command + prefix_len + host_len, suffix, sizeof(suffix));

    log_d("建立 TCP 连接，主机=%.*s", host_length, host);
    return device_net_at_exec(command, 8000U);
}



device_net_status device_net_CIPSEND(size_t request_length)
{
    if (request_length == 0U)
        return DEVICE_NET_INVALID_ARG;

    char command[128];
    /* 整形拼接进字符型字符串 */
    int len = snprintf(command, sizeof(command), "AT+CIPSEND=%lu\r\n", (unsigned long)request_length);

    if ((len < 0) || ((size_t)len >= sizeof(command)))
        return DEVICE_NET_INVALID_ARG;
    log_d("声明发送长度=%lu 字节", (unsigned long)request_length);
    s_wait_prompt = true;
    device_net_status result = device_net_at_exec(command, 300U);
    s_wait_prompt = false;
    return result;
}



static void device_net_rx_request(const uint8_t *data, uint16_t len, void *arg)
{
    (void)arg;
    static char line[128];
    static size_t line_len;
    static uint32_t remaining; /* 当前 +IPD 尚未收完的数据字节数 */
    static bool send_ok;

    /* 执行函数在启动接收前调用一次，清理上次留下的半包。 */
    if (data == NULL)
    {
        line_len = 0U;
        remaining = 0U;
        send_ok = false;
        s_response_len = 0U;
        s_response[0] = '\0';
        return;
    }

    for (uint16_t i = 0U; (i < len) && (s_at_state == AT_WAITING); ++i)
    {
        const uint8_t byte = data[i];
        if (remaining > 0U)
        {
            /* IPD 内部不识别 OK、换行等控制信息，只按长度保存 TCP 数据。 */
            s_response[s_response_len++] = (char)byte;
            s_response[s_response_len] = '\0';
            --remaining;
            continue;
        }

        if ((byte == ':') && (line_len >= 5U) && (memcmp(line, "+IPD,", 5U) == 0))
        {
            /* 仅处理单连接、主动接收、CIPDINFO=0 的 +IPD,<长度>: 格式。 */
            uint32_t count = 0U;
            if (line_len == 5U)
            {
                s_at_state = AT_FAIL;
                return;
            }
            for (size_t j = 5U; j < line_len; ++j)
            {
                if ((line[j] < '0') || (line[j] > '9') || (count > (UINT32_MAX - (uint32_t)(line[j] - '0')) / 10U))
                {
                    s_at_state = AT_FAIL;
                    return;
                }
                count = count * 10U + (uint32_t)(line[j] - '0');
            }
            if (count > sizeof(s_response) - 1U - s_response_len)
            {
                s_at_state = AT_FAIL;
                return;
            }
            remaining = count;
            line_len = 0U;
        }
        else if (byte == '\n')
        {
            if ((line_len > 0U) && (line[line_len - 1U] == '\r'))
                --line_len;
            line[line_len] = '\0';
            if (strcmp(line, "SEND OK") == 0)
                send_ok = true; /* 发出成功，仍要继续等待服务器数据。 */
            else if (strcmp(line, "CLOSED") == 0)
                s_at_state = (send_ok && (s_response_len > 0U)) ? AT_OK : AT_FAIL;
            else if ((strcmp(line, "ERROR") == 0) || (strcmp(line, "FAIL") == 0) ||
                     (strcmp(line, "SEND FAIL") == 0) || (strncmp(line, "busy", 4U) == 0))
                s_at_state = AT_FAIL;
            line_len = 0U;
        }
        else
        {
            if (line_len >= sizeof(line) - 1U)
            {
                s_at_state = AT_FAIL;
                return;
            }
            line[line_len++] = (char)byte;
        }
    }
}

static device_net_status device_net_request(const char *request, size_t len)
{
    device_net_status result = DEVICE_NET_ERROR;
    const uint32_t timeout_ms = 10000U;

    if ((request == NULL) || (len == 0U))
    {
    	log_e("HTTP 请求为空或长度为零");
        return DEVICE_NET_INVALID_ARG;
    }
    /* 兼容外层传 sizeof(request)，不要把结尾的 '\0' 发给 ESP8266。 */
    if (request[len - 1U] == '\0')
        --len;
    if ((len == 0U) || (len > UINT16_MAX))
    {
        log_e("HTTP 请求长度超出串口发送范围");
        return DEVICE_NET_INVALID_ARG;
    }

    port_enter_critical();
    if (s_at_state != AT_REQUEST)
    {
        port_exit_critical();
        log_e("设备忙");
        return DEVICE_NET_BUSY;
    }
    s_http_response_valid = false;
    request_weather_valid = false;
    request_time_valid = false;
    s_at_state = AT_WAITING; /* 共用串口占用标记，防止普通 AT 指令插入。 */
    port_exit_critical();

    if (uart_port_init() != PORT_OK)
    {
        s_at_state = AT_IDLE;
        log_e("设备IO错误");
        return DEVICE_NET_IO_ERROR;
    }
    if (uart_port_rx_is_active())
    {
        s_at_state = AT_IDLE;
        log_e("设备忙");
        return DEVICE_NET_BUSY;
    }

    device_net_rx_request(NULL, 0U, NULL);
    /* 此函数不发送 CIPSEND；调用前模块确保已进入可发送 request 的状态。 */
    if (uart_port_rx_start(s_dma_buf_request, sizeof(s_dma_buf_request), device_net_rx_request, NULL) != PORT_OK)
    {
        s_at_state = AT_IDLE;
        log_e("设备IO错误");
        return DEVICE_NET_IO_ERROR;
    }

    const uint32_t started_ms = port_time_ms();
    const port_status_t tx_status = uart_port_write(UART_PORT_CH_NET, (const uint8_t *)request, (uint16_t)len, 1000U);
    if (tx_status == PORT_OK)
    {
    	// 等待request发送完成
        while ((s_at_state == AT_WAITING) && ((uint32_t)(port_time_ms() - started_ms) < timeout_ms) && uart_port_rx_is_active())
            port_delay_ms(1U);

        if (!uart_port_rx_is_active())
        {
        	log_e("设备IO错误");
            result = DEVICE_NET_IO_ERROR;
        }
        else if ((uint32_t)(port_time_ms() - started_ms) >= timeout_ms)
        {
            log_e("设备超时");
            result = DEVICE_NET_TIMEOUT;
        }
        else if (s_at_state == AT_OK)
        {
        	log_d("HTTP 响应接收完成");
            result = DEVICE_NET_OK;
        }
    }
    else
        result = (tx_status == PORT_TIMEOUT) ? DEVICE_NET_TIMEOUT : DEVICE_NET_IO_ERROR;

    if (uart_port_rx_stop() != PORT_OK)
        result = DEVICE_NET_IO_ERROR;

    /* 已停止接收，在主循环中输出诊断信息。 */
    log_i("请求结果=%d，接收状态=%d，HTTP字节数=%u",
          (int)result,
          (int)s_at_state,
          (unsigned int)s_response_len);

    s_at_state = AT_IDLE;
    s_http_response_valid = (result == DEVICE_NET_OK);
    /* s_response / s_response_len 留存原始 HTTP 响应，供后续解析。 */
    return result;
}

device_net_status device_net_REQUEST(void)
{
    request_weather_valid = false;
    request_time_valid = false;
    s_http_response_valid = false;
    if (s_at_state != AT_REQUEST)
    {
        log_e("尚未进入数据发送状态，请先执行 CIPSEND");
        return DEVICE_NET_ERROR;
    }

    return device_net_request(request, sizeof(request) - 1U);
}

static uint8_t days_in_month(int year, uint8_t month)
{
    static const uint8_t days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if ((month < 1U) || (month > 12U))
        return 0U;
    if ((month == 2U) && (year % 4 == 0) && ((year % 100 != 0) || (year % 400 == 0)))
        return 29U;
    return days[month - 1U];
}

void get_request_rcv_buff(void)
{
    int parsed_len = -1;
    int temperature, humidity;
    request_weather_valid = false;
    request_time_valid = false;
    /* 仅解析已完整接收的响应，避免把超时后的半包或 AT 响应当成数据。 */
    if ((s_at_state != AT_IDLE) || !s_http_response_valid)
    {
        log_w("没有完整的 HTTP 响应可供解析");
        return;
    }
    const char *body = strstr(s_response, "\r\n\r\n");
    if (body == NULL)
    {
        log_e("未找到 HTTP 正文");
        return;
    }
    /* 当前接口只接受 200 响应，错误页面不能提供天气或校时数据。 */
    if ((strncmp(s_response, "HTTP/1.1 200 ", 13U) != 0) &&
        (strncmp(s_response, "HTTP/1.0 200 ", 13U) != 0))
    {
        log_e("HTTP 状态码异常");
        return;
    }
    request_time[0] = '\0';
    request_year = 0;
    request_month = request_day = request_hour = request_min = request_sec = 0U;
    log_d("HTTP 原始响应：\n%s", s_response);



    /* 固定匹配 Date 字段，并确保它位于 HTTP 响应头内。 */
    const char *date = strstr(s_response, "\r\nDate:");
    if ((date != NULL) && (date < body))
    {
        const char *date_start = date + 7; /* 跳过 \r\nDate: */
        const char *date_end = strstr(date_start, "\r\n");
        if ((date_end != NULL) && (date_end <= body))
        {
            while ((date_start < date_end) && ((*date_start == ' ') || (*date_start == '\t')))
                ++date_start;
            while ((date_end > date_start) && ((date_end[-1] == ' ') || (date_end[-1] == '\t')))
                --date_end;

            size_t time_len = (size_t)(date_end - date_start);
            if ((time_len > 0U) && (time_len < sizeof(request_time)))
            {
                memcpy(request_time, date_start, time_len);
                request_time[time_len] = '\0';
                log_d("HTTP Date：%s", request_time);
            }
        }
    }
    if (request_time[0] == '\0')
        log_w("未获取到有效的 HTTP Date 时间");
    else
    {
        /* 例如 Fri, 23 Oct 2026 08:30:45 GMT，先校验 GMT 日期。 */
        static const char *const months[] = {
            "Jan", "Feb", "Mar", "Apr", "May", "Jun",
            "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
        };
        char month_name[4];
        int year, day, hour, min, sec;
        int date_len = -1;
        uint8_t month = 0U;
        int date_count = sscanf(request_time, "%*3s, %2d %3s %4d %2d:%2d:%2d GMT%n",
                                &day, month_name, &year, &hour, &min, &sec, &date_len);
        if (date_count == 6)
        {
            for (uint8_t i = 0U; i < 12U; ++i)
            {
                if (strcmp(month_name, months[i]) == 0)
                {
                    month = i + 1U;
                    break;
                }
            }
        }
        if ((date_count == 6) && (date_len >= 0) && (request_time[date_len] == '\0') &&
            (year >= 1) && (year <= 9999) && (month != 0U) &&
            (day >= 1) && (day <= days_in_month(year, month)) && (hour >= 0) && (hour <= 23) &&
            (min >= 0) && (min <= 59) && (sec >= 0) && (sec <= 59))
        {
            /* GMT 转北京时间时，处理跨日、跨月和跨年 */
            hour += 8;
            if (hour >= 24)
            {
                hour -= 24;
                if (++day > days_in_month(year, month))
                {
                    day = 1;
                    if (++month > 12U)
                    {
                        month = 1U;
                        ++year;
                    }
                }
            }
            if (year > 9999)
            {
                log_w("北京时间超出支持的年份范围");
            }
            else
            {
                request_year = year;
                request_month = month;
                request_day = (uint8_t)day;
                request_hour = (uint8_t)hour;
                request_min = (uint8_t)min;
                request_sec = (uint8_t)sec;
                request_time_valid = true;
                log_i("时间：%04d-%02u-%02u %02u:%02u:%02u",request_year, (unsigned int)request_month, (unsigned int)request_day,(unsigned int)request_hour, (unsigned int)request_min, (unsigned int)request_sec);
            }
        }
        else
            log_w("HTTP Date 时间格式错误：%s", request_time);
    }

    body += 4; /* 跳过响应头与正文之间的空行。 */

    /* 限制整数宽度，避免异常长数字溢出；单位须匹配请求的摄氏度格式。 */
    int count = sscanf(body, "%4d°C|%3d%%%n", &temperature, &humidity, &parsed_len);

    if ((count != 2) || (parsed_len < 0) || (humidity < 0) || (humidity > 100))
    {
        log_e("温湿度正文格式错误");
        log_d("温湿度原始正文：%s", body);
        return;
    }

    const char *tail = body + parsed_len;
    while ((*tail == ' ') || (*tail == '\t') || (*tail == '\r') || (*tail == '\n'))
        ++tail;
    if (*tail != '\0')
    {
        log_e("温湿度正文包含多余内容");
        return;
    }
    request_temp = temperature;
    request_humi = (uint8_t)humidity;
    request_weather_valid = true;
    log_i("温度：%d 摄氏度，湿度：%d%%", request_temp, request_humi);
}

device_net_status device_net_get_weather(int *temp, uint8_t *humi)
{
    if ((temp == NULL) || (humi == NULL))
        return DEVICE_NET_INVALID_ARG;
    if (!request_weather_valid)
        return DEVICE_NET_ERROR;

    *temp = request_temp;
    *humi = request_humi;
    return DEVICE_NET_OK;
}

device_net_status device_net_get_time(int *year, uint8_t *month, uint8_t *day, uint8_t *hour, uint8_t *min, uint8_t *sec)
{
    if ((year == NULL) || (month == NULL) || (day == NULL) || (hour == NULL) || (min == NULL) || (sec == NULL))
        return DEVICE_NET_INVALID_ARG;
    if (!request_time_valid)
        return DEVICE_NET_ERROR;

    *year = request_year;
    *month = request_month;
    *day = request_day;
    *hour = request_hour;
    *min = request_min;
    *sec = request_sec;
    return DEVICE_NET_OK;
}
