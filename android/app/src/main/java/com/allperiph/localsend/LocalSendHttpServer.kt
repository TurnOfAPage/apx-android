package com.allperiph.localsend

import org.json.JSONObject
import java.io.BufferedReader
import java.io.InputStream
import java.io.OutputStream
import java.net.ServerSocket
import java.net.Socket
import java.util.concurrent.atomic.AtomicBoolean

/**
 * 极简 HTTP/1.1 server，仅覆盖 LocalSend v2 所需端点（每连接一线程，Connection: close）。
 * 不引第三方依赖；错误路径一律文本 4xx，对端对不认识的路径会自行降级。
 */
class LocalSendHttpServer(
    private val port: Int,
    private val handler: Handler,
) {
    interface Handler {
        /** GET info 与 register 的响应体（设备信息） */
        fun deviceInfo(): JSONObject

        /** POST prepare-upload：接受清单，返回 {sessionId, files:{id:token}} */
        fun prepareUpload(body: JSONObject): JSONObject

        /** POST upload：打开目标文件输出流；未知 session/file 返回 null → 404 */
        fun openUpload(sessionId: String, fileId: String): OutputStream?

        /** 传输结束（ok=写完并关闭）或出错时回调 */
        fun finishUpload(sessionId: String, fileId: String, ok: Boolean)

        /** POST cancel */
        fun cancel(sessionId: String?)
    }

    private val running = AtomicBoolean(false)
    private var serverSocket: ServerSocket? = null
    private var acceptThread: Thread? = null

    fun start() {
        if (!running.compareAndSet(false, true)) return
        val ss = ServerSocket(port)
        serverSocket = ss
        acceptThread = Thread({
            while (running.get()) {
                val socket = try { ss.accept() } catch (_: Throwable) { break }
                Thread { handle(socket) }.apply { isDaemon = true; start() }
            }
        }, "LocalSend-HTTP-Accept").apply { isDaemon = true; start() }
    }

    fun stop() {
        if (!running.compareAndSet(true, false)) return
        try { serverSocket?.close() } catch (_: Throwable) {}
        serverSocket = null
    }

    private fun handle(socket: Socket) {
        var finished = false
        try {
            socket.soTimeout = 120_000
            val input = socket.getInputStream()
            val reader = input.bufferedReader(Charsets.UTF_8)
            val requestLine = reader.readLine() ?: return
            val parts = requestLine.split(" ")
            if (parts.size < 3) return
            val method = parts[0].uppercase()
            val path = parts[1].substringBefore('?')
            val query = if ('?' in parts[1]) parts[1].substringAfter('?') else ""

            val headers = HashMap<String, String>()
            while (true) {
                val line = reader.readLine() ?: break
                if (line.isEmpty()) break
                val idx = line.indexOf(':')
                if (idx > 0) headers[line.substring(0, idx).trim().lowercase()] = line.substring(idx + 1).trim()
            }
            val contentLength = headers["content-length"]?.toLongOrNull() ?: 0L

            val out = socket.getOutputStream()
            when {
                method == "GET" && path == LocalSendProtocol.PATH_INFO ->
                    respondJson(out, 200, handler.deviceInfo())

                method == "POST" && path == LocalSendProtocol.PATH_REGISTER -> {
                    readBodyText(reader, contentLength)
                    respondJson(out, 200, handler.deviceInfo())
                }

                method == "POST" && path == LocalSendProtocol.PATH_PREPARE -> {
                    val body = JSONObject(readBodyText(reader, contentLength))
                    respondJson(out, 200, handler.prepareUpload(body))
                }

                (method == "POST" || method == "PUT") && path == LocalSendProtocol.PATH_UPLOAD -> {
                    val sessionId = queryParam(query, "sessionId") ?: ""
                    val fileId = queryParam(query, "fileId") ?: ""
                    val target = handler.openUpload(sessionId, fileId)
                    if (target == null) {
                        handler.finishUpload(sessionId, fileId, false)
                        respondText(out, 404, "unknown session/file")
                    } else {
                        var ok = true
                        try {
                            copyExactly(input, target, contentLength)
                            target.close()
                        } catch (_: Throwable) {
                            ok = false
                            try { target.close() } catch (_: Throwable) {}
                        }
                        handler.finishUpload(sessionId, fileId, ok)
                        if (ok) respondText(out, 204, "") else respondText(out, 500, "write failed")
                    }
                }

                method == "POST" && path == LocalSendProtocol.PATH_CANCEL -> {
                    readBodyText(reader, contentLength)
                    handler.cancel(queryParam(query, "sessionId"))
                    respondText(out, 204, "")
                }

                else -> respondText(out, 404, "not found")
            }
            finished = true
        } catch (_: Throwable) {
            // 连接级异常：对端超时/中断属常态，静默即可
        } finally {
            if (!finished) { /* 未正常响应时不做额外处理 */ }
            try { socket.close() } catch (_: Throwable) {}
        }
    }

    private fun readBodyText(reader: BufferedReader, length: Long): String {
        if (length <= 0) return ""
        val chars = CharArray(length.toInt().coerceAtMost(1 shl 20))
        var read = 0
        while (read < chars.size) {
            val n = reader.read(chars, read, chars.size - read)
            if (n < 0) break
            read += n
        }
        return String(chars, 0, read)
    }

    /** 按 Content-Length 精确搬字节：body 尚未被 reader 缓冲区消费时才成立；
     *  我们仅对 upload（流式 body）走此路径且此前未读过 body 文本 */
    private fun copyExactly(input: InputStream, target: OutputStream, length: Long) {
        val buf = ByteArray(64 * 1024)
        var remaining = length
        if (remaining <= 0) {
            // 无 Content-Length（chunked 等）：本地网络不常见，尽力读到 EOF 不支持，直接视为失败
            throw IllegalStateException("upload without content-length")
        }
        while (remaining > 0) {
            val n = input.read(buf, 0, minOf(buf.size.toLong(), remaining).toInt())
            if (n < 0) throw java.io.EOFException("truncated body")
            target.write(buf, 0, n)
            remaining -= n
        }
    }

    private fun queryParam(query: String, key: String): String? =
        query.split('&').firstOrNull { "${key}=" in it }?.substringAfter("${key}=")

    private fun respondJson(out: OutputStream, code: Int, json: JSONObject) {
        val body = json.toString().toByteArray(Charsets.UTF_8)
        val status = when (code) { 200 -> "OK"; 204 -> "No Content"; else -> "Error" }
        val head = "HTTP/1.1 $code $status\r\n" +
            "Content-Type: application/json; charset=utf-8\r\n" +
            "Content-Length: ${body.size}\r\n" +
            "Connection: close\r\n\r\n"
        out.write(head.toByteArray(Charsets.US_ASCII)); out.write(body); out.flush()
    }

    private fun respondText(out: OutputStream, code: Int, text: String) {
        val body = text.toByteArray(Charsets.UTF_8)
        val status = when (code) { 200 -> "OK"; 204 -> "No Content"; 404 -> "Not Found"; 500 -> "Server Error"; else -> "Error" }
        val head = "HTTP/1.1 $code $status\r\n" +
            "Content-Length: ${body.size}\r\n" +
            "Connection: close\r\n\r\n"
        out.write(head.toByteArray(Charsets.US_ASCII)); out.write(body); out.flush()
    }
}
