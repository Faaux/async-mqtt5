#ifndef ASYNC_MQTT5_MQTT_CLIENT_HPP
#define ASYNC_MQTT5_MQTT_CLIENT_HPP

#include <boost/system/error_code.hpp>

#include <async_mqtt5/error.hpp>
#include <async_mqtt5/types.hpp>

#include <async_mqtt5/impl/client_service.hpp>
#include <async_mqtt5/impl/publish_send_op.hpp>
#include <async_mqtt5/impl/read_message_op.hpp>
#include <async_mqtt5/impl/subscribe_op.hpp>
#include <async_mqtt5/impl/unsubscribe_op.hpp>

namespace async_mqtt5 {

namespace asio = boost::asio;

/**
 * \brief MQTT client used to connect and communicate with a Broker.
 *
 * \tparam StreamType Type of the underlying transport protocol used to transfer
 * the stream of bytes between the Client and the Broker. The transport must be
 * ordered and lossless.
 * \tparam TlsContext Type of the context object used in TLS/SSL connections.
 */
template <
	typename StreamType,
	typename TlsContext = std::monostate
>
class mqtt_client {
public:
	/// The executor type associated with the client.
	using executor_type = typename StreamType::executor_type;
private:
	using stream_type = StreamType;
	using tls_context_type = TlsContext;

	static constexpr auto read_timeout = std::chrono::seconds(5);

	using client_service_type = detail::client_service<
		stream_type, tls_context_type
	>;
	using clisvc_ptr = std::shared_ptr<client_service_type>;
	clisvc_ptr _svc_ptr;

public:
	/**
	 * \brief Constructs a Client with given parameters.
	 *
	 * \param ex An executor that will be associated with the Client.
	 * \param cnf 
	 * \param tls_context A context object used in TLS/SLL connection.
	 */
	explicit mqtt_client(
		const executor_type& ex,
		const std::string& cnf,
		tls_context_type tls_context = {}
	) :
		_svc_ptr(std::make_shared<client_service_type>(
			ex, cnf, std::move(tls_context)
		))
	{}

	/**
	 * \brief Constructs a client with given parameters.
	 *
	 * \tparam ExecutionContext Type of a concrete execution context.
	 * \param context Execution context whose executor will be associated with the Client.
	 * \param cnf 
	 * \param tls_context A context object used in TLS/SLL connection.
	 *
	 * \par Precondition
	 * `std::is_convertible_v<ExecutionContext&, asio::execution_context&>`
	 */
	template <typename ExecutionContext>
	requires (std::is_convertible_v<ExecutionContext&, asio::execution_context&>)
	explicit mqtt_client(
		ExecutionContext& context,
		const std::string& cnf,
		TlsContext tls_context = {}
	) :
		mqtt_client(context.get_executor(), cnf, std::move(tls_context))
	{}

	/**
	 * \brief Destructor.
	 *
	 * \details Automatically calls \ref mqtt_client::cancel.
	 */
	~mqtt_client() {
		cancel();
	}

	/**
	 * \brief Get the executor associated with the object.
	 */
	executor_type get_executor() const noexcept {
		return _svc_ptr->get_executor();
	}


	/**
	 * \brief Get the context object used in TLS/SSL connection.
	 *
	 * \par Precondition
	 * `!std::is_same_v<TlsContext, std::monostate>`.
	 */
	decltype(auto) tls_context()
	requires (!std::is_same_v<TlsContext, std::monostate>) {
		return _svc_ptr->tls_context();
	}

	/**
	 * \brief Start the Client.
	 */
	void run() {
		_svc_ptr->open_stream();
		detail::ping_op { _svc_ptr }
			.perform(read_timeout - std::chrono::seconds(1));
		detail::read_message_op { _svc_ptr }.perform();
		detail::sentry_op { _svc_ptr }.perform();
	}

	// TODO: channel cancel
	/**
	 * \brief Cancel all asynchronous operations.
	 *
	 * \details All outstanding operations will complete
	 * with `boost::asio::error::operation_aborted`.
	 */
	void cancel() {
		get_executor().execute([svc_ptr = _svc_ptr]() {
			svc_ptr->cancel();
		});
	}

	/**
	 * \brief Assign a \ref will Message.
	 *
	 * \details The \ref will Message that the Broker should publish
	 * after the Network Connection is closed and it is not
	 * closed normally.
	 */
	mqtt_client& will(will will) {
		_svc_ptr->will(std::move(will));
		return *this;
	}

	/**
	 * \brief Assign credentials that will be used to connect to a Broker.
	 *
	 * \details Credentials consist of a unique Client Identifier and, optionally,
	 * a User Name and Password.
	 */
	mqtt_client& credentials(
		std::string client_id,
		std::string username = "", std::string password = ""
	) {
		_svc_ptr->credentials(
			std::move(client_id),
			std::move(username), std::move(password)
		);
		return *this;
	}

	/**
	 * \brief Assign a list of Brokers that the Client will attempt to connect to.
	 *
	 * \param hosts List of Broker addresses and ports.
	 * Address and ports are separated with a colon `:` while
	 * pairs of addresses and ports are separated with a comma `,`.
	 * \param default_port Default port to connect to in case the port is not
	 * explicitly specified in the hosts list.
	 *
	 * \details Examples of a valid hosts string:\n
	 *		- broker1:1883, broker2, broker3:1883\n
	 *		- broker1
	 */
	mqtt_client& brokers(std::string hosts, uint16_t default_port = 1883) {
		_svc_ptr->brokers(std::move(hosts), default_port);
		return *this;
	}


	// TODO: doc props
	/**
	 * \brief Send a PUBLISH packet to Broker to transport an
	 * Application Message.
	 *
	 * \tparam qos_type The \ref qos_e level of assurance for delivery.
	 * \param topic Identification of the information channel to which
	 * Payload data is published.
	 * \param payload The Application Message that is being published.
	 * \param retain The \ref retain_e flag.
	 * \param props PUBLISH properties. 
	 * \param token Completion token that will be used to produce a
	 * completion handler, which will be called when the operation completed.
	 *
	 * \par Completion signature
	 * The completion signature for this operation depends on the \ref qos_e specified:\n
	 *
	 *	- `qos_e::at_most_once`:
	 *		\code
	 *			void (
	 *				boost::system::error_code	// Result of operation
	 *			)
	 *		\endcode
	 *
	 *	- `qos_e::at_least_once`:
	 *		\code
	 *			void (
	 *				boost::system::error_code,	// Result of operation.
	 *				async_mqtt5::reason_code,	// Reason Code received from Broker.
	 *				puback_props	// Properties received in the PUBACK packet.
	 *			)
	 *		\endcode
	 *
	 *	- `qos_e::exactly_once`:
	 *		\code
	 *			void (
	 *				boost::system::error_code,	// Result of operation.
	 *				async_mqtt5::reason_code,	// Reason Code received from Broker.
	 *				pubcomp_props	// Properties received in the PUBCOMP packet.
	 *			)
	 *		\endcode
	 *
	 *	\par Error codes
	 *	The list of all possible error codes that this operation can finish with:\n
	 *		- `boost::system::errc::errc_t::success`\n
	 *		- `boost::asio::operation_aborted` \n
	 *		- \link client::error::pid_overrun \endlink
	 *		- \link client::error::qos_not_supported \endlink
	 *		- \link client::error::retain_not_available \endlink
	 *		- \link client::error::topic_alias_maximum_reached \endlink
	 */
	template <qos_e qos_type, typename CompletionToken>
	decltype(auto) async_publish(
		std::string topic, std::string payload,
		retain_e retain, const publish_props& props,
		CompletionToken&& token
	) {
		using Signature = detail::on_publish_signature<qos_type>;

		auto initiate = [] (
			auto handler, std::string topic, std::string payload,
			retain_e retain, const publish_props& props,
			const clisvc_ptr& svc_ptr
		) {
			detail::publish_send_op<
				client_service_type, decltype(handler), qos_type
			> { svc_ptr, std::move(handler) }
				.perform(
					std::move(topic), std::move(payload),
					retain, props
				);
		};

		return asio::async_initiate<CompletionToken, Signature>(
			std::move(initiate), token,
			std::move(topic), std::move(payload), retain, props, _svc_ptr
		);
	}

	// TODO: perhaps there is a way to not copy documentation (\copybrief, \copydetails)
	/**
	 * \brief Send a SUBSCRIBE packet to Broker to create a subscription
	 * to one or more Topics of interest.
	 *
	 * \details After the subscription has been established, the Broker will send
	 * PUBLISH packets to the Client to forward Application Messages that were published
	 * to Topics that the Client subscribed to. The PUBLISH packets can be received
	 * with \ref mqtt_client::async_receive function.
	 *
	 * \param topics A list of \ref subscribe_topic of interest.
	 * \param props SUBSCRIBE properties.
	 * \param token Completion token that will be used to produce a
	 * completion handler, which will be called when the operation completed.
	 *
	 * \par Completion signature
	 * The completion signature for this operation:
	 *	\code
	 *		void (
	 *			boost::system::error_code,	// Result of operation.
	 *			std::vector<reason_code>,	// Vector of Reason Codes indicating
	 *										// the subscription result for each Topic
	 *										// in the SUBSCRIBE packet.
	 *			suback_props,	// Properties received in the SUBACK packet.
	 *		)
	 *	\endcode
	 *
	 *	\par Error codes
	 *	The list of all possible error codes that this operation can finish with:\n
	 *		- `boost::system::errc::errc_t::success`\n
	 *		- `boost::asio::operation_aborted` \n
	 *		- \link client::error::pid_overrun \endlink
	 */
	template <typename CompletionToken>
	decltype(auto) async_subscribe(
		const std::vector<subscribe_topic>& topics,
		const subscribe_props& props,
		CompletionToken&& token
	) {
		using Signature = void (
			error_code, std::vector<reason_code>, suback_props
		);

		auto initiate = [] (
			auto handler, const std::vector<subscribe_topic>& topics,
			const subscribe_props& props, const clisvc_ptr& impl
		) {
			detail::subscribe_op { impl, std::move(handler) }
				.perform(topics, props);
		};

		return asio::async_initiate<CompletionToken, Signature>(
			std::move(initiate), token, topics, props, _svc_ptr
		);
	}

	/**
	 * \brief Send a SUBSCRIBE packet to Broker to create a subscription
	 * to one Topics of interest.
	 *
	 * \details After the subscription has been established, the Broker will send
	 * PUBLISH packets to the Client to forward Application Messages that were published
	 * to Topics that the Client subscribed to. The PUBLISH packets can be received
	 * with \ref mqtt_client::async_receive function.
	 *
	 * \param topic A \ref subscribe_topic of interest.
	 * \param props SUBSCRIBE properties.
	 * \param token Completion token that will be used to produce a
	 * completion handler, which will be called when the operation completed.
	 *
	 * \par Completion signature
	 * The completion signature for this operation:
	 *	\code
	 *		void (
	 *			boost::system::error_code,	// Result of operation.
	 *			std::vector<reason_code>,	// Vector of Reason Codes containing the
	 *										// single subscription result for the Topic
	 *										// in the SUBSCRIBE packet.
	 *			suback_props,	// Properties received in the SUBACK packet.
	 *		)
	 *	\endcode
	 *
	 *	\par Error codes
	 *	The list of all possible error codes that this operation can finish with:\n
	 *		- `boost::system::errc::errc_t::success`\n
	 *		- `boost::asio::operation_aborted` \n
	 *		- \link client::error::pid_overrun \endlink
	 */
	template <typename CompletionToken>
	decltype(auto) async_subscribe(
		const subscribe_topic& topic, const subscribe_props& props,
		CompletionToken&& token
	) {
		return async_subscribe(
			std::vector<subscribe_topic> { topic }, props,
			std::forward<CompletionToken>(token)
		);
	}


	/**
	 * \brief Send an UNSUBSCRIBE packet to Broker to unsubscribe from one
	 * or more Topics.
	 *
	 * \note The Client MAY receive PUBLISH packets with Application
	 * Messages from Topics the Client just unsubscribed to if
	 * they were buffered for delivery on the Broker side beforehand.
	 *
	 * \param topics List of Topics to unsubscribe from.
	 * \param props UNSUBSCRIBE properties.
	 * \param token Completion token that will be used to produce a
	 * completion handler, which will be called when the operation completed.
	 *
	 * \par Completion signature
	 * The completion signature for this operation:
	 *	\code
	 *		void (
	 *			boost::system::error_code, // Result of operation.
	 *			std::vector<reason_code>,	// Vector of Reason Codes indicating
	 *										// the result of unsubscribe operation
	 *										// for each Topic in the UNSUBSCRIBE packet.
	 *			unsuback_props, // Properties received in the UNSUBACK packet.
	 *		)
	 *	\endcode
	 *
	 *	\par Error codes
	 *	The list of all possible error codes that this operation can finish with:\n
	 *		- `boost::system::errc::errc_t::success`\n
	 *		- `boost::asio::operation_aborted` \n
	 *		- \link client::error::pid_overrun \endlink
	 */
	template <typename CompletionToken>
	decltype(auto) async_unsubscribe(
		const std::vector<std::string>& topics,	const unsubscribe_props& props,
		CompletionToken&& token
	) {
		using Signature = void (
			error_code, std::vector<reason_code>, unsuback_props
		);

		auto initiate = [](
			auto handler,
			const std::vector<std::string>& topics,
			const unsubscribe_props& props,	const clisvc_ptr& impl
		) {
			detail::unsubscribe_op { impl, std::move(handler) }
				.perform(topics, props);
		};

		return asio::async_initiate<CompletionToken, Signature>(
			std::move(initiate), token, topics, props, _svc_ptr
		);
	}

	/**
	 * \brief Send an UNSUBSCRIBE packet to Broker to unsubscribe
	 * from one Topic.
	 *
	 * \note The Client MAY receive PUBLISH packets with Application
	 * Messages from Topics the Client just unsubscribed to if
	 * they were buffered for delivery on the Broker side beforehand.
	 *
	 * \param topic Topic to unsubscribe from.
	 * \param props UNSUBSCRIBE properties.
	 * \param token Completion token that will be used to produce a
	 * completion handler, which will be called when the operation completed.
	 *
	 * \par Completion signature
	 * The completion signature for this operation:
	 *	\code
	 *		void (
	 *			boost::system::error_code, // Result of operation.
	 *			std::vector<reason_code>,	// Vector of Reason Codes containing
	 *										// the result of unsubscribe operation
	 *										// for the Topic in the UNSUBSCRIBE packet.
	 *			unsuback_props, // Properties received in the UNSUBACK packet.
	 *		)
	 *	\endcode
	 *
	 *	\par Error codes
	 *	The list of all possible error codes that this operation can finish with:\n
	 *		- `boost::system::errc::errc_t::success`\n
	 *		- `boost::asio::operation_aborted` \n
	 *		- \link client::error::pid_overrun \endlink
	 */
	template <typename CompletionToken>
	decltype(auto) async_unsubscribe(
		const std::string& topic, const unsubscribe_props& props,
		CompletionToken&& token
	) {
		return async_unsubscribe(
			std::vector<std::string> { topic }, props,
			std::forward<CompletionToken>(token)
		);
	}

	/**
	 * \brief Asynchronously receive an Application Message.
	 *
	 * \details The Client will receive and complete deliveries for all the
	 * PUBLISH packets received from the Broker throughout its lifetime.
	 * The Client will store them internally in order they were delivered.
	 * Calling this function will attempt to receive an Application Message
	 * from internal storage.
	 *
	 * \note The completion handler will be only invoked if an error occurred
	 * or there is a pending Application Message.
	 *
	 * \param token Completion token that will be used to produce a
	 * completion handler, which will be called when the operation completed.
	 *
	 * \par Completion signature
	 * The completion signature for this operation:
	 *	\code
	 *		void (
	 *			boost::system::error_code, // Result of operation.
	 *			std::string,	// Topic, the origin of the Application Message.
	 *			std::string,	// Payload, the content of the Application Message.
	 *			publish_props, // Properties received in the PUBLISH packet.
	 *		)
	 *	\endcode
	 *
	 *	\par Error codes
	 *	The list of all possible error codes that this operation can finish with:\n
	 *		- `boost::system::errc::errc_t::success`\n
	 *		- `boost::experimental::error::channel_cancelled`
	 */
	template <typename CompletionToken>
	decltype(auto) async_receive(CompletionToken&& token) {
		// Sig = void (error_code, std::string, std::string, publish_props)
		return _svc_ptr->async_channel_receive(
			std::forward<CompletionToken>(token)
		);
	}

	/**
	 * \brief Disconnect the Client.
	 *
	 * \details Send a DISCONNECT packet to the Broker with a Reason Code
	 * describing the reason for disconnection.
	 *
	 * \note This function has terminal effects and will close the Client.
	 *
	 * \param reason_code Reason Code to notify
	 * the Broker of the reason for disconnection.
	 * \param props DISCONNECT properties.
	 * \param token Completion token that will be used to produce a
	 * completion handler, which will be called when the operation completed.
	 *
	 * \par Completion signature
	 * The completion signature for this operation:
	 *	\code
	 *		void (
	 *			boost::system::error_code // Result of operation.
	 *		)
	 *	\endcode
	 *
	 *	\par Error codes
	 *	The list of all possible error codes that this operation can finish with:\n
	 *		- `boost::system::errc::errc_t::success`\n
	 *		- `boost::asio::operation_aborted`\n
	 *		- `boost::asio::no_recovery`\n
	 */
	template <typename CompletionToken>
	decltype(auto) async_disconnect(
		disconnect_rc_e reason_code, const disconnect_props& props,
		CompletionToken&& token
	) {
		return detail::async_disconnect(
			reason_code, props, true, _svc_ptr,
			std::forward<CompletionToken>(token)
		);
	}

	/**
	 * \brief Disconnect the Client.
	 *
	 * \details Send a DISCONNECT packet to the Broker with a Reason Code
	 * \ref reason_codes::normal_disconnection describing
	 * the reason for disconnection.
	 *
	 * \note This function has terminal effects and will close the Client.
	 *
	 * \param token Completion token that will be used to produce a
	 * completion handler, which will be called when the operation completed.
	 *
	 * \par Completion signature
	 * The completion signature for this operation:
	 *	\code
	 *		void (
	 *			boost::system::error_code // Result of operation.
	 *		)
	 *	\endcode
	 *
	 *	\par Error codes
	 *	The list of all possible error codes that this operation can finish with:\n
	 *		- `boost::system::errc::errc_t::success`\n
	 *		- `boost::asio::operation_aborted`\n
	 *		- `boost::asio::no_recovery`\n
	 */
	template <typename CompletionToken>
	decltype(auto) async_disconnect(CompletionToken&& token) {
		return async_disconnect(
			disconnect_rc_e::normal_disconnection,
			disconnect_props {}, std::forward<CompletionToken>(token)
		);
	}
};


} // end namespace async_mqtt5

#endif // !ASYNC_MQTT5_MQTT_CLIENT_HPP
