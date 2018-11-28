#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>

static void mytest_upstream_finalize_request( ngx_http_request_t* r, ngx_int_t rc); 
static ngx_int_t mytest_upstream_create_request( ngx_http_request_t* r);
static ngx_int_t mytest_upstream_process_header( ngx_http_request_t* r);
static void* ngx_http_mytest_create_loc_conf( ngx_conf_t* cf) ;
static char* ngx_http_mytest_merge_loc_conf( ngx_conf_t* cf, void* parent, void* child) ;
static ngx_int_t ngx_http_mytest_handler( ngx_http_request_t* r) ;
static char* ngx_http_mytest_upstream(ngx_conf_t* cf, ngx_command_t* cmd, void* conf);
static ngx_str_t  ngx_http_proxy_hide_headers[] = {
    ngx_string("Date"),
    ngx_string("Server"),
    ngx_string("X-Pad"),
    ngx_string("X-Accel-Expires"),
    ngx_string("X-Accel-Redirect"),
    ngx_string("X-Accel-Limit-Rate"),
    ngx_string("X-Accel-Buffering"),
    ngx_string("X-Accel-Charset"),
    ngx_null_string
};

typedef struct{ 
	ngx_http_status_t status; 
	ngx_str_t	backendServer;
}ngx_http_mytest_ctx_t;
typedef struct{ 
	ngx_http_upstream_conf_t upstream; 
} ngx_http_mytest_conf_t;

static ngx_command_t ngx_http_mytest_upstream_commands[]={ 
	
	{
	ngx_string("upstream_test"),
	NGX_HTTP_LOC_CONF| NGX_CONF_TAKE1,
	ngx_http_mytest_upstream,
	//ngx_conf_set_msec_slot,
	NGX_HTTP_LOC_CONF_OFFSET,
	/*给出 conn_timeout 成员 在 ngx_http_mytest_conf_t 结构 体中 的 偏移 字节数*/ 
	offsetof( ngx_http_mytest_conf_t, upstream.connect_timeout),
	NULL
	},
	ngx_null_command 
};

static ngx_http_module_t ngx_http_mytest_upstream_module_ctx={ 
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
	//NULL,
	ngx_http_mytest_create_loc_conf,
	/*create location configuration*/
	// NULL
	ngx_http_mytest_merge_loc_conf 
	  /*merge location configuration*/
};

ngx_module_t  ngx_http_mytest_upstream_module= {
NGX_MODULE_V1,
&ngx_http_mytest_upstream_module_ctx,
ngx_http_mytest_upstream_commands,
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
static ngx_int_t mytest_upstream_create_request( ngx_http_request_t* r) {
 /*发往 google 上游 服务器 的 请求 很 简单, 就是 模仿 正常 的 搜索 请求, 以/ search? q=…… 的 URL 来 发起 搜索 请求。 backendQueryLine 中的% V 等 转化 格式 的 用法, 可 参见 表 4- 7*/ 
 static ngx_str_t backendQueryLine= ngx_string(" GET/ search? q=% V HTTP/ 1.1\r\nHost： www.google.com\r\nConnection： close\r\n\r\n");
 ngx_int_t queryLineLen= backendQueryLine.len+ r->args.len- 2;
 /*必须 在 内存 池 中 申请 内存, 这有 以下 两点 好处： 一个 好处 是, 在 网络 情况 不佳 的 情况下, 向上游 服务器 发送 请求 时, 可能 需要 epoll 多次 调度 send 才能 发送 完成, 这时 必须 保证 这段 内存 不会 被 释放;
 另一个 好处 是, 在 请求 结束 时, 这段 内存 会被 自动 释放, 降低 内存 泄漏 的 可能*/ 
 ngx_buf_t* b= ngx_create_temp_buf( r->pool, queryLineLen);
 if( b== NULL)
		return NGX_ERROR;
 //last 要 指向 请求 的 末尾 b->last= b->pos+ queryLineLen;
 //作用 相当于 snprintf, 只是 它 支持 表 4- 7 中 列出 的 所有 转换 格式 ngx_snprintf( b->pos, queryLineLen, (char*) backendQueryLine.data,& r->args);
 /*r->upstream->request_bufs 是 一个 ngx_chain_t 结构, 它 包 含着 要 发送 给 上游 服务器 的 请求*/ 
 r->upstream->request_bufs= ngx_alloc_chain_link( r->pool);
 if( r->upstream->request_bufs== NULL) return NGX_ERROR;
 //request_bufs 在这里 只 包含 1 个 ngx_buf_t 缓冲区 r->upstream->request_bufs->buf= b;
 r->upstream->request_bufs->next= NULL;
 r->upstream->request_sent= 0;
 r->upstream->header_sent= 0;
 //header_hash 不可 以为 0 r->header_hash= 1;
 return NGX_OK;
 }
static ngx_int_t mytest_process_status_line( ngx_http_request_t* r) {
size_t len;
 ngx_int_t rc;
 ngx_http_upstream_t* u;
 //上下 文中 才会 保存 多次 解析 HTTP 响应 行的 状态, 下面 首先 取出 请求 的 上下文 
 ngx_http_mytest_ctx_t* ctx= ngx_http_get_module_ctx( r, ngx_http_mytest_upstream_module);
 if( ctx== NULL){ return NGX_ERROR;
 } u= r->upstream;
 /*HTTP 框架 提供 的 ngx_http_parse_status_line 方法 可以 解析 HTTP 响应 行, 它的 输入 就是 收到 的 字符 流 和 上下文 中的 ngx_http_status_t 结构*/ 
 rc= ngx_http_parse_status_line( r,& u->buffer,& ctx->status);
 //返回 NGX_AGAIN 时, 表示 还没 有解 析出 完整 的 HTTP 响应 行, 需要 接收 更多 的 字符 流 再进 行 解析*/ 
 if( rc== NGX_AGAIN){ return rc;
 } 
 //返回 NGX_ERROR 时, 表示 没有 接收 到 合法 的 HTTP 响应 行 
 if( rc== NGX_ERROR){ ngx_log_error( NGX_LOG_ERR, r->connection->log, 0, "upstream sent no valid HTTP/ 1.0 header");
 r->http_version= NGX_HTTP_VERSION_9;
 u->state->status= NGX_HTTP_OK;
 return NGX_OK;
 } /*以下 表示 在 解析 到 完整 的 HTTP 响应 行 时, 会 做 一些 简单 的 赋值 操作, 将 解 析出 的 信息
设置 到 r->upstream->headers_in 结构 体中。 当 upstream 解析 完 所有 的 包头 时, 会把 headers_in 中的 成员 设置 到 将要 向下 游 发送 的 r->headers_out 结构 体中, 也就是说, 现在 用户 向 headers_in 中 设置 的 信息, 最终 都会 发往 下游 客户 端。 为什么 不 直接 设置 r->headers_out 而要 多此一举 呢？ 因为 upstream 希望 能够 按照 ngx_http_upstream_conf_t 配置 结构 体中 的 hide_headers 等 成员 对 发往 下游 的 响应 头部 做 统一 处理*/ 
	if( u->state){ u->state->status= ctx->status.code;
 } 
 u->headers_in.status_n= ctx->status.code;
 len= ctx->status.end- ctx->status.start;
 u->headers_in.status_line.len= len;
 u->headers_in.status_line.data= ngx_pnalloc( r->pool, len);
 if( u->headers_in.status_line.data== NULL){ return NGX_ERROR;
 } ngx_memcpy( u->headers_in.status_line.data, ctx->status.start, len);
 /*下一步 将 开始 解析 HTTP 头部。 设置 process_header 回 调 方法 为 mytest_upstream_process_header, 之后 再 收到 的 新 字符 流 将由 mytest_upstream_process_header 解析*/ 
 u->process_header= mytest_upstream_process_header;
 /*如果 本次 收到 的 字符 流 除了 HTTP 响应 行 外, 还有 多余 的 字符, 那么 将由 mytest_upstream_process_header 方法 解析*/ 
 return mytest_upstream_process_header( r);
 }
static ngx_int_t mytest_upstream_process_header( ngx_http_request_t* r) { 
	ngx_int_t rc;
 ngx_table_elt_t *h;
 ngx_http_upstream_header_t *hh;
 ngx_http_upstream_main_conf_t* umcf;
 /*这里 将 upstream 模块 配置 项 ngx_http_upstream_main_conf_t 取出 来, 目的 只有 一个, 就是 对 将要 转发 给 下游 客户 端 的 HTTP 响应 头部 进行 统一 处理。 该 结构 体中 存储 了 需要 进行 统一 处理 的 HTTP 头部 名称 和 回 调 方法*/ 
 umcf= ngx_http_get_module_main_conf( r, ngx_http_upstream_module);
 //循环 地 解析 所有 的 HTTP 头部 
 for(;;){ /*HTTP 框架 提供 了 基础 性的 ngx_http_parse_header_line 方法, 它 用于 解析 HTTP 头部*/
	rc= ngx_http_parse_header_line( r,& r->upstream->buffer, 1);
 //返回 NGX_OK 时, 表示 解析 出 一行 HTTP 头部 
 if( rc== NGX_OK){ //向 headers_in.headers 这个 ngx_list_t 链 表中 添加 HTTP 头部 
 	h= ngx_list_push(& r->upstream->headers_in.headers);
 	if( h== NULL){ 
 		return NGX_ERROR;
 } //下面 开始 构造 刚刚 添加 到 headers 链 表中 的 HTTP 头部 h->hash= r->header_hash;
 h->key.len= r->header_name_end- r->header_name_start;
 h->value.len= r->header_end- r->header_start;
 //必须 在 内存 池 中 分配 存放 HTTP 头部 的 内存 空间 
 h->key.data= ngx_pnalloc( r->pool, h->key.len+ 1+ h->value.len+ 1+ h->key.len);
 if( h->key.data== NULL){ 
 	return NGX_ERROR;
 } 
 h->value.data= h->key.data+ h->key.len+ 1;
 h->lowcase_key= h->key.data+ h->key.len+ 1+ h->value.len+ 1;
 ngx_memcpy( h->key.data, r->header_name_start, h->key.len);
 h->key.data[ h->key.len]='\0';
 ngx_memcpy( h->value.data, r->header_start, h->value.len);
 h->value.data[ h->value.len]='\0';
 if( h->key.len== r->lowcase_index){ 
 		ngx_memcpy( h->lowcase_key, r->lowcase_header, h->key.len);
 	}else{ 
 		ngx_strlow( h->lowcase_key, h->key.data, h->key.len);
 	}
//upstream 模块 会对 一些 HTTP 头部 做 特殊 处理 
	hh= ngx_hash_find(& umcf->headers_in_hash, h->hash, h->lowcase_key, h->key.len);
 if( hh&& hh->handler( r, h, hh->offset)!= NGX_OK){ 
 		return NGX_ERROR;
 	} 
 	continue;
 } /*返回 NGX_HTTP_PARSE_HEADER_DONE 时, 表示 响应 中 所有 的 HTTP 头部 都 解析 完毕, 接下来 再 接 收到 的 都将 是 HTTP 包 体*/ if( rc== NGX_HTTP_PARSE_HEADER_DONE){ /*如果 之前 解析 HTTP 头部 时 没有 发现 server 和 date 头部, 那么 下面 会 根据 HTTP 协议 规范 添加 这 两个 头部*/ 
 if( r->upstream->headers_in.server== NULL){ h= ngx_list_push(& r->upstream->headers_in.headers);
 if( h== NULL){ return NGX_ERROR;
 } 
 h->hash= ngx_hash( ngx_hash( ngx_hash( ngx_hash( ngx_hash('s','e'),'r'),'v'),'e'),'r');
 ngx_str_set(& h->key,"Server");
 ngx_str_null(& h->value);
 h->lowcase_key=( u_char*)" server";
 } if( r->upstream->headers_in.date== NULL){ h= ngx_list_push(& r->upstream->headers_in.headers);
 if( h== NULL){ return NGX_ERROR;
 }
h->hash= ngx_hash( ngx_hash( ngx_hash('d','a'),'t'),'e');
 ngx_str_set(& h->key,"Date");
 ngx_str_null(& h->value);
 h->lowcase_key=( u_char*)"date";
 } return NGX_OK;
 } /*如果 返回 NGX_AGAIN, 则 表示 状态 机 还没有 解析 到 完整 的 HTTP 头部, 此时 要求 upstream 模块 继续 接收 新的 字符 流, 然后 交由 process_header 回 调 方法 解析*/ 
 if( rc== NGX_AGAIN){ return NGX_AGAIN;
 } 
 //其他 返回 值 都是 非法 的 
 ngx_log_error( NGX_LOG_ERR, r->connection->log, 0, "upstream sent invalid header");
 return NGX_HTTP_UPSTREAM_INVALID_HEADER;
 } 
 }
static void mytest_upstream_finalize_request( ngx_http_request_t* r, ngx_int_t rc) 
	{ 
		ngx_log_error( NGX_LOG_DEBUG, r->connection->log, 0, "mytest_upstream_finalize_request");
 }
//static ngx_int_t ngx_http_mytest_upstream_handler( ngx_http_request_t* r) {
static ngx_int_t ngx_http_mytest_handler( ngx_http_request_t* r) {
//首先 建立 HTTP 上下文 结构 体 
 ngx_http_mytest_ctx_t* myctx= ngx_http_get_module_ctx( r, ngx_http_mytest_upstream_module);
 if( myctx== NULL) { 
	 myctx= ngx_palloc( r->pool, sizeof( ngx_http_mytest_ctx_t));
 	if( myctx== NULL) { 
 		return NGX_ERROR;
 	} 
	//将 新建 的 上下文 与 请求 关联 起来 
 	ngx_http_set_ctx( r, myctx, ngx_http_mytest_upstream_module);
 } /*对 每 1 个 要 使用 upstream 的 请求, 必须 调用 且 只能 调用 1 次 ngx_http_upstream_create 方法, 它 会 初始化 r->upstream 成员*/ 
 
 if( ngx_http_upstream_create( r)!= NGX_OK){ 
	 ngx_log_error( NGX_LOG_ERR, r->connection->log, 0," ngx_http_upstream_create() failed");
 	return NGX_ERROR;
 } //得到 配置 结构 体 
 ngx_http_mytest_conf_t* mycf=( ngx_http_mytest_conf_t*) ngx_http_get_module_loc_conf( r, ngx_http_mytest_upstream_module);
 ngx_http_upstream_t* u= r->upstream;
 //这里 用 配置文件 中的 结构 体 来 赋 给 r->upstream->conf 成员 
 u->conf=& mycf->upstream;
 //决定 转 发包 体 时 使用 的 缓冲区 
 u->buffering= mycf->upstream.buffering;
 //以下 代码 开始 初始化 resolved 结构 体, 用来 保存 上游 服务器 的 地址
 u->resolved=( ngx_http_upstream_resolved_t*) ngx_pcalloc( r->pool, sizeof( ngx_http_upstream_resolved_t));
 if( u->resolved== NULL){ ngx_log_error( NGX_LOG_ERR, r->connection->log, 0, "ngx_pcalloc resolved error.% s.", strerror( errno));
 return NGX_ERROR;
 } //这里 的 上游 服务器 就是 www.google.com 
 static struct sockaddr_in backendSockAddr;
 struct hostent* pHost= gethostbyname(( char*)"www.google.com");
 if( pHost== NULL) { ngx_log_error( NGX_LOG_ERR, r->connection->log, 0, "gethostbyname fail.% s", strerror( errno));
 return NGX_ERROR;
 } //访问 上游 服务器 的 80 端口 
 backendSockAddr.sin_family= AF_INET;
 backendSockAddr.sin_port= htons(( in_port_t) 80);
 char* pDmsIP= inet_ntoa(*( struct in_addr*)( pHost->h_addr_list[ 0]));
 backendSockAddr.sin_addr.s_addr= inet_addr( pDmsIP);
 myctx->backendServer.data=( u_char*) pDmsIP;
 myctx->backendServer.len= strlen( pDmsIP);
 //将 地址 设置 到 resolved 成员 中 
 u->resolved->sockaddr=( struct sockaddr*)& backendSockAddr;
 u->resolved->socklen= sizeof( struct sockaddr_in);
 u->resolved->naddrs= 1;
 //设置 3 个 必须 实现 的 回 调 方法, 也就是 5.3.3 节 ～ 5.3.5 节 中 实现 的 3 个 方法
u->create_request= mytest_upstream_create_request;
 u->process_header= mytest_process_status_line;
 u->finalize_request= mytest_upstream_finalize_request;
 //这里 必须 将 count 成员 加 1, 参见 5.1.5 节 
 r->main->count++;
 //启动 upstream 
 ngx_http_upstream_init( r);
 //必须 返回 NGX_DONE 
 return NGX_DONE;
 }
static char* ngx_http_mytest_upstream(ngx_conf_t* cf, ngx_command_t* cmd, void* conf){ 
	ngx_http_core_loc_conf_t* clcf;
/*首先 找到 upstream_conn_timeout 配置 项 所属 的 配置 块, clcf 看上去 像是 location 块 内 的 数据 结构, 其实不然, 它 可以 是 main、 srv 或者 loc 级别 配置 项, 也就是说, 在 每个 http{} 和 server{} 内 也都 有一个 ngx_http_core_loc_conf_t 结构 体*/
	clcf= ngx_http_conf_get_module_loc_conf(cf,ngx_http_core_module);
/*HTTP 框架 在 处理 用户 请求 进行 到 NGX_HTTP_CONTENT_PHASE 阶段 时, 如果 请求 的 主机 域名、 URI 与 mytest 配置 项 所在 的 配置 块 相 匹配, 就 将 调用 我们 实现 的 ngx_http_mytest_handler 方法 处理 这个 请求*/ 
	clcf->handler= ngx_http_mytest_handler;
	return NGX_CONF_OK;
}

static void* ngx_http_mytest_create_loc_conf( ngx_conf_t* cf) { 
	ngx_http_mytest_conf_t* mycf;
	mycf=( ngx_http_mytest_conf_t*) 
	ngx_pcalloc( cf->pool, sizeof( ngx_http_mytest_conf_t)); 
	if( mycf== NULL){ 
	 	return NULL;
	} 
	/*以下 简单 的 硬 编码 ngx_http_upstream_conf_t 结构 中的 各 成员, 如 超时 时间, 都 设为 1 分钟, 这也 是 HTTP 反向 代理 模块 的 默认值*/
	mycf->upstream.connect_timeout= 60000; 
	mycf->upstream.send_timeout= 60000; 
	mycf->upstream.read_timeout= 60000; 
	mycf->upstream.store_access= 0600; 
	/*实际上, buffering 已经 决定了 将以 固定 大小 的 内存 作为 缓冲区 来 转发 上游 的 响应 包 体, 这块 固定 缓冲区 的 大小 就是 buffer_size。 如果 buffering 为 1, 就会 使用 更多 的 内存 缓存 来不及 发往 下游 的 响应。 例如, 最多 使用 bufs.num 个 缓冲区 且 每个 缓冲区 大小 为 bufs.size。 另外, 还会 使用 临时 文件, 临时 文件 的 最大 长度 为 max_temp_file_size*/ 
	mycf->upstream.buffering= 0; 
	mycf->upstream.bufs.num= 8; 
	mycf->upstream.bufs.size= ngx_pagesize; 
	mycf->upstream.buffer_size= ngx_pagesize; 
	mycf->upstream.busy_buffers_size= 2* ngx_pagesize; 
	mycf->upstream.temp_file_write_size= 2* ngx_pagesize; 
	mycf->upstream.max_temp_file_size= 1024* 1024* 1024;
	/*upstream 模块 要求 hide_headers 成员 必须 要 初始化( upstream 在 解析 完 上游 服务器 返回 的 包头 时, 会 调用 ngx_http_upstream_process_headers 方法 按照 hide_headers 成员 将 本应 转发 给 下游 的 一些 HTTP 头部 隐藏), 这里 将它 赋 为 NGX_CONF_UNSET_PTR, 这是 为了 在 merge 合并 配置 项 方法 中 使用 upstream 模块 提供 的 ngx_http_upstream_hide_headers_hash 方法 初始化 hide_headers 成员*/ 
	mycf->upstream.hide_headers= NGX_CONF_UNSET_PTR; 
	mycf->upstream.pass_headers= NGX_CONF_UNSET_PTR; 
	return mycf;
}
static char* ngx_http_mytest_merge_loc_conf( ngx_conf_t* cf, void* parent, void* child) 
{ 
	ngx_http_mytest_conf_t* prev=( ngx_http_mytest_conf_t*) parent;
 	ngx_http_mytest_conf_t* conf=( ngx_http_mytest_conf_t*) child;
 	ngx_hash_init_t hash;
 	hash.max_size= 100;
 	hash.bucket_size= 1024;
 	hash.name=" proxy_headers_hash";
 	if( ngx_http_upstream_hide_headers_hash( cf,&conf->upstream, &prev->upstream, ngx_http_proxy_hide_headers,&hash) !=NGX_OK) {
		return NGX_CONF_ERROR;
 	} 
 	return NGX_CONF_OK;
}
