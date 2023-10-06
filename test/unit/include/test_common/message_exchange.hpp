#ifndef ASYNC_MQTT5_TEST_MESSAGE_EXCHANGE_HPP
#define ASYNC_MQTT5_TEST_MESSAGE_EXCHANGE_HPP

#include <chrono>
#include <deque>
#include <optional>
#include <string>
#include <vector>

#include "test_common/delayed_op.hpp"

namespace async_mqtt5::test {

using error_code = boost::system::error_code;
using time_stamp = std::chrono::time_point<std::chrono::steady_clock>;
using duration = time_stamp::duration;

class msg_exchange;
class broker_message;

inline duration after(duration d) { return d; }

using namespace std::chrono_literals;

namespace detail {

class stream_message {
	error_code _ec;
	duration _after { 0 };
	std::vector<uint8_t> _content;

public:

	template <typename ...Args>
	stream_message(error_code ec, duration after, Args&& ...args) :
		_ec(ec), _after(after)
	{
		(_content.insert(_content.end(), args.begin(), args.end()), ...);
	}

	stream_message(const stream_message&) = delete;
	stream_message(stream_message&&) = default;

	template <typename Executor>
	auto to_operation(const Executor& ex) {
		return delayed_op<error_code, std::vector<uint8_t>> {
			ex, _after, _ec, std::move(_content)
		};
	}
};


} // end namespace detail


class client_message {
	msg_exchange* _owner;

	error_code _write_ec;
	duration _complete_after { 0 };
	std::vector<std::string> _expected_packets;

	std::vector<detail::stream_message> _replies;

public:
	template <typename ...Args>
	client_message(msg_exchange* owner, Args&&... args) :
		_owner(owner),
		_expected_packets({ std::forward<Args>(args)... })
	{}

	client_message(const client_message&) = delete;
	client_message(client_message&&) = default;

	client_message& complete_with(error_code ec, duration af) {
		_write_ec = ec;
		_complete_after = af;
		return *this;
	}

	template <typename ...Args>
	client_message& reply_with(Args&& ...args) {
		// just to allow duration to be the last parameter
		auto t = std::make_tuple(std::forward<Args>(args)...);
		using Tuple = decltype(t);

		return[&]<auto... I>(std::index_sequence<I...>) -> client_message& {
			return reply_with_impl(
				std::get<std::tuple_size_v<Tuple> -1>(t),
				std::get<I>(t)...
			);
		}(std::make_index_sequence<std::tuple_size_v<Tuple> -1>{});
	}

	template <typename ...Args>
	client_message& expect(Args&& ...args);

	template <typename ...Args>
	broker_message& send(Args&& ...args);

	template <typename Executor>
	decltype(auto) write_completion(const Executor& ex) const {
		return delayed_op<error_code>(ex, _complete_after, _write_ec);
	}

	template <typename Executor>
	decltype(auto) pop_reply_ops(const Executor& ex) {
		std::vector<delayed_op<error_code, std::vector<uint8_t>>> ret;
		std::transform(
			_replies.begin(), _replies.end(), std::back_inserter(ret),
			[&ex](auto& r) { return r.to_operation(ex); }
		);
		_replies.clear();
		return ret;
	}

private:

	template <typename ...Args>
	requires (std::is_same_v<std::remove_cvref_t<Args>, std::string> && ...)
	client_message& reply_with_impl(duration af, Args&& ...args) {
		_replies.emplace_back(
			error_code {}, af, std::forward<Args>(args)...
		);
		return *this;
	}

	client_message& reply_with_impl(duration af, error_code ec) {
		_replies.emplace_back(ec, af);
		return *this;
	}
};

class broker_message {
	msg_exchange* _owner;
	detail::stream_message _message;

public:
	template <typename ...Args>
	broker_message(
		msg_exchange* owner, error_code ec, duration af, Args&&... args
	) :
		_owner(owner), _message(ec, af, std::forward<Args>(args) ...)
	{}

	broker_message(const broker_message&) = delete;
	broker_message(broker_message&&) = default;

	template <typename ...Args>
	client_message& expect(Args&& ...args);

	template <typename ...Args>
	broker_message& send(Args&& ...args);

	template <typename Executor>
	decltype(auto) pop_send_op(const Executor& ex) {
		return _message.to_operation(ex);
	}
};


class msg_exchange {
	std::deque<client_message> _to_broker;
	std::vector<broker_message> _from_broker;

public:
	template <typename ...Args>
	requires (std::is_same_v<std::remove_cvref_t<Args>, std::string> && ...)
	client_message& expect(Args&& ...args) {
		_to_broker.emplace_back(this, std::forward<Args>(args)...);
		return _to_broker.back();
	}

	template <typename ...Args>
	broker_message& send(Args&& ...args) {
		// just to allow duration to be the last parameter
		auto t = std::make_tuple(std::forward<Args>(args)...);
		using Tuple = decltype(t);

		return[&]<auto... I>(std::index_sequence<I...>) -> broker_message& {
			return send_impl(
				std::get<std::tuple_size_v<Tuple> -1>(t),
				std::get<I>(t)...
			);
		}(std::make_index_sequence<std::tuple_size_v<Tuple> -1>{});
	}

	std::optional<client_message> pop_reply_action() {
		if (_to_broker.empty())
			return std::nullopt;

		auto rv = std::move(_to_broker.front());
		_to_broker.pop_front();
		return rv;
	}

	template <typename Executor>
	auto pop_broker_ops(const Executor& ex) {
		std::vector<delayed_op<error_code, std::vector<uint8_t>>> ret;
		std::transform(
			_from_broker.begin(), _from_broker.end(), std::back_inserter(ret),
			[&ex](auto& s) { return s.pop_send_op(ex); }
		);
		_from_broker.clear();
		return ret;
	}

private:

	template <typename ...Args>
	requires (std::is_same_v<std::remove_cvref_t<Args>, std::string> && ...)
	broker_message& send_impl(duration after, Args&& ...args) {
		_from_broker.emplace_back(
			this, error_code {}, after, std::forward<Args>(args)...
		);
		return _from_broker.back();
	}

	broker_message& send_impl(duration after, error_code ec) {
		_from_broker.emplace_back(this, ec, after);
		return _from_broker.back();
	}
};

template <typename ...Args>
client_message& client_message::expect(Args&& ...args) {
	return _owner->expect(std::forward<Args>(args)...);
}

template <typename ...Args>
broker_message& client_message::send(Args&& ...args) {
	return _owner->send(std::forward<Args>(args)...);
}

template <typename ...Args>
client_message& broker_message::expect(Args&& ...args) {
	return _owner->expect(std::forward<Args>(args)...);
}

template <typename ...Args>
broker_message& broker_message::send(Args&& ...args) {
	return _owner->send(std::forward<Args>(args)...);
}


} // end namespace async_mqtt5::test

#endif // ASYNC_MQTT5_TEST_MESSAGE_EXCHANGE_HPP
