
#include "AdapterTls.h"

namespace Socket
{
	AdapterTls::AdapterTls(const Socket &sock, ::gnutls_priority_t priority_cache, ::gnutls_certificate_credentials_t x509_cred) noexcept
	{
		// https://leandromoreira.com.br/2015/10/12/how-to-optimize-nginx-configuration-for-http2-tls-ssl/
		// https://www.gnutls.org/manual/html_node/False-Start.html

		#ifdef GNUTLS_ENABLE_FALSE_START
			constexpr int flags = GNUTLS_SERVER | GNUTLS_ENABLE_FALSE_START;
		#else
			constexpr int flags = GNUTLS_SERVER;
		#endif

		::gnutls_init(&this->session, flags);
		::gnutls_priority_set(this->session, priority_cache);
		::gnutls_credentials_set(this->session, GNUTLS_CRD_CERTIFICATE, x509_cred);

		::gnutls_certificate_server_set_request(this->session, GNUTLS_CERT_IGNORE);

		::gnutls_transport_set_int2(this->session, sock.get_handle(), sock.get_handle() );

		char h2[] = "h2";
		char http11[] = "http/1.1";

		const ::gnutls_datum_t protocols[] {
			{ reinterpret_cast<unsigned char *>(h2), sizeof(h2) - 1 },
			{ reinterpret_cast<unsigned char *>(http11), sizeof(http11) - 1 },
		};

		::gnutls_alpn_set_protocols(this->session, protocols, sizeof(protocols) / sizeof(::gnutls_datum_t), 0);
	}

	AdapterTls::AdapterTls(const ::gnutls_session_t session) noexcept : session(session)
	{

	}

	bool AdapterTls::handshake() noexcept
	{
		int ret;

		do
		{
			ret = ::gnutls_handshake(this->session);
		}
		while (ret < 0 && ::gnutls_error_is_fatal(ret) == 0);

		if (ret < 0)
		{
			Socket sock(this->get_handle() );

			sock.close();
			::gnutls_deinit(this->session);

			return false;
		}

		return true;
	}

	long AdapterTls::nonblock_send_all(const void *buf, const size_t length, const std::chrono::milliseconds &timeout) const noexcept
	{
	//	size_t record_size = ::gnutls_record_get_max_size(this->session);
		size_t record_size = length;

		if (0 == record_size)
		{
			return -1;
		}

		Socket sock(this->get_handle() );

	//	::gnutls_record_set_timeout(this->session, static_cast<unsigned int>(timeout.count() ) );

		size_t total = 0;

		while (total < length)
		{
			if (record_size > length - total)
			{
				record_size = length - total;
			}

		//	const long send_size = ::gnutls_record_send(this->session, reinterpret_cast<const uint8_t *>(buf) + total, record_size);

			long send_size = 0;

			do
			{
				sock.nonblock_send_sync();
			}
			while (GNUTLS_E_AGAIN == (send_size = ::gnutls_record_send(this->session, reinterpret_cast<const uint8_t *>(buf) + total, record_size) ) );

			if (send_size < 0)
			{
				return send_size;
			}

			total += send_size;
		}

		return static_cast<long>(total);
	}

	System::native_socket_type AdapterTls::get_handle() const noexcept
	{
		return static_cast<System::native_socket_type>(::gnutls_transport_get_int(this->session) );
	}

	::gnutls_session_t AdapterTls::get_tls_session() const noexcept
	{
		return this->session;
	}

	Adapter *AdapterTls::copy() const noexcept
	{
		return new AdapterTls(this->session);
	}

	long AdapterTls::nonblock_recv(void *buf, const size_t length, const std::chrono::milliseconds &timeout) const noexcept
	{
	//	::gnutls_record_set_timeout(this->session, static_cast<const unsigned int>(timeout.count() ) );

		Socket sock(this->get_handle() );
		sock.nonblock_recv_sync();

		return ::gnutls_record_recv(this->session, buf, length);
	}

	long AdapterTls::nonblock_send(const void *buf, const size_t length, const std::chrono::milliseconds &timeout) const noexcept
	{
		return this->nonblock_send_all(buf, length, timeout);
	}

	void AdapterTls::close() noexcept
	{
		Socket sock(this->get_handle() );

		// Wait for send all data to client
		sock.nonblock_send_sync();

		::gnutls_bye(this->session, GNUTLS_SHUT_RDWR);

		sock.close();

		::gnutls_deinit(this->session);
	}
};
