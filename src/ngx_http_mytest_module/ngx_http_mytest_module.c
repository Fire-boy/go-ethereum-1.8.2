#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>


typedef struct{ 
	ngx_http_status_t status; 
}ngx_http_mytest_ctx_t;
typedef struct{ 
	ngx_http_upstream_conf_t upstream; 
} ngx_http_mytest_conf_t;

ngx_int_t ngx_http_mytest_handler (ngx_http_request_t * r ){
	
	/*必须 是 GET 或者 HEAD 方法, 否则 返回 405 Not Allowed */
	if(!(r->method&(NGX_HTTP_GET| NGX_HTTP_HEAD)))
	{
		return NGX_HTTP_NOT_ALLOWED;
	}
	
	/*丢弃 请求 中的 包 体 */
	ngx_int_t rc = ngx_http_discard_request_body(r);
	if( rc != NGX_OK ) { 
		return rc;
	}
	/*设置 返回 的 Content- Type。 注意, ngx_str_t 有一个 很 方便 的 初始化 宏 ngx_string, 它 可以 把 ngx_str_t 的 data 和 len 成员 都 设置 好*/
	ngx_str_t type= ngx_string(" text/ plain");
	//ngx_str_t type= ngx_string(" html/ plain");
	/*返回 的 包 体内 容*/
	ngx_str_t response= ngx_string(" Hello World!");
	/*ngx_str_t response= ngx_string("<html>    \
			<head>				\
			<title>Welcome to nginx!</title>\
			<style>		\
			    body {	\
			            width: 35em;  \
				            margin: 0 auto; \
					            font-family: Tahoma, Verdana, Arial, sans-serif; \
						        }	\
							</style>	\
							</head>	\
							<body>	\
							<h1>Welcome to wgw test!</h1>	\
							<p> my test html!</p>	\
							<p>For online documentation and support please refer to	\
							<a href=\"http://nginx.org/\">nginx.org</a>.<br/>	\
							Commercial support is available at	\
							<a href=\"http://nginx.com/\">nginx.com</a>.</p>\
							<p><em>Thank you for using nginx.</em></p>	\
							</body>	\
							</html>");*/

	/*设置 返回 状态 码 */
	r->headers_out.status= NGX_HTTP_OK;
	/*响应 包 是有 包 体 内容 的, 需要 设置 Content- Length 长度 */
	r->headers_out.content_length_n= response.len;
	/*设置 Content- Type */
	r->headers_out.content_type= type;
	/*发送 HTTP 头部 */
	rc= ngx_http_send_header(r);
	if(rc== NGX_ERROR|| rc > NGX_OK|| r->header_only){ 
		return rc;
	} 
	/*构造 ngx_buf_t 结构 体 准备 发送 包 体*/
	 ngx_buf_t* b;
	 b= ngx_create_temp_buf(r->pool, response.len);
	if(b== NULL){ 
		return NGX_HTTP_INTERNAL_SERVER_ERROR;
	} 
	/*将 Hello World 复制 到 ngx_buf_t 指向 的 内存 中 */
	ngx_memcpy(b->pos, response.data, response.len);
	/*注意, 一定 要 设置 好 last 指针 */
	b->last= b->pos+ response.len;
	/*声明 这是 最后 一块 缓冲区 */
	b->last_buf= 1;
	/*构造 发 送时 的 ngx_chain_t 结构 体 */
	ngx_chain_t out;
	/*赋值 */
	out.buf = b;
	/*设置 next 为 NULL */
	out.next= NULL;
/*最后 一步 为 发送 包 体, 发送 结束 后 HTTP 框架 会 调用 ngx_http_finalize_request 方法 结束 请求*/
	return ngx_http_output_filter(r,&out);
}	

static char* ngx_http_mytest(ngx_conf_t* cf, ngx_command_t* cmd, void* conf){ 
	ngx_http_core_loc_conf_t* clcf;
/*首先 找到 mytest 配置 项 所属 的 配置 块, clcf 看上去 像是 location 块 内 的 数据 结构, 其实不然, 它 可以 是 main、 srv 或者 loc 级别 配置 项, 也就是说, 在 每个 http{} 和 server{} 内 也都 有一个 ngx_http_core_loc_conf_t 结构 体*/
	clcf= ngx_http_conf_get_module_loc_conf(cf,ngx_http_core_module);
/*HTTP 框架 在 处理 用户 请求 进行 到 NGX_HTTP_CONTENT_PHASE 阶段 时, 如果 请求 的 主机 域名、 URI 与 mytest 配置 项 所在 的 配置 块 相 匹配, 就 将 调用 我们 实现 的 ngx_http_mytest_handler 方法 处理 这个 请求*/ 
	clcf->handler= ngx_http_mytest_handler;
	return NGX_CONF_OK;
}

static ngx_command_t ngx_http_mytest_commands[]={ 
	{	ngx_string("mytest"),
	       	NGX_HTTP_MAIN_CONF| NGX_HTTP_SRV_CONF| NGX_HTTP_LOC_CONF| NGX_HTTP_LMT_CONF| NGX_CONF_NOARGS,
		ngx_http_mytest,
		NGX_HTTP_LOC_CONF_OFFSET,
		0,
		NULL
	},
	ngx_null_command 
};

static ngx_http_module_t ngx_http_mytest_module_ctx={ 
	NULL,
	/*preconfiguration*/
	NULL,
	/*postconfiguration*/ 
	NULL,
	/*create main configuration*/ 
	NULL,
	/*init main configuration*/ 
	NULL,
	/*create server configuration*/ 
	NULL,
	/*merge server configuration*/ 
	NULL,
	/*create location configuration*/
	 NULL 
	  /*merge location configuration*/
};

ngx_module_t  ngx_http_mytest_module= {
NGX_MODULE_V1,
&ngx_http_mytest_module_ctx,
ngx_http_mytest_commands,
NGX_HTTP_MODULE,
NULL,                                  /* exit master */
NULL,
NULL,
NULL,
NULL,
NULL,
NULL,
NGX_MODULE_V1_PADDING
};
