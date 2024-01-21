/**
 * Canary - A free and open-source MMORPG server emulator
 * Copyright (©) 2019-2022 OpenTibiaBR <opentibiabr@outlook.com>
 * Repository: https://github.com/opentibiabr/canary
 * License: https://github.com/opentibiabr/canary/blob/main/LICENSE
 * Contributors: https://github.com/opentibiabr/canary/graphs/contributors
 * Website: https://docs.opentibiabr.com/
 */

#pragma once

#include "game/scheduling/dispatcher.hpp"
#include <opentelemetry/exporters/ostream/metric_exporter_factory.h>
#include <opentelemetry/sdk/metrics/export/periodic_exporting_metric_reader_factory.h>
#include <opentelemetry/exporters/prometheus/exporter_factory.h>
#include <opentelemetry/exporters/prometheus/exporter_options.h>
#include <opentelemetry/metrics/provider.h>
#include <opentelemetry/sdk/metrics/aggregation/default_aggregation.h>
#include <opentelemetry/sdk/metrics/aggregation/histogram_aggregation.h>
#include <opentelemetry/sdk/metrics/push_metric_exporter.h>
#include <opentelemetry/sdk/metrics/aggregation/base2_exponential_histogram_indexer.h>
#include <opentelemetry/sdk/metrics/meter.h>
#include <opentelemetry/sdk/metrics/meter_provider.h>
#include <opentelemetry/sdk/metrics/meter_provider_factory.h>
#include <opentelemetry/sdk/metrics/view/instrument_selector_factory.h>
#include <opentelemetry/sdk/metrics/view/meter_selector_factory.h>
#include <opentelemetry/sdk/metrics/view/view_factory.h>

namespace metrics_sdk = opentelemetry::sdk::metrics;
namespace common = opentelemetry::common;
namespace metrics_exporter = opentelemetry::exporter::metrics;
namespace metrics_api = opentelemetry::metrics;

constexpr std::string_view methodName(const char* s) {
	std::string_view prettyFunction(s);
	size_t bracket = prettyFunction.rfind("(");
	size_t space = prettyFunction.rfind(" ", bracket) + 1;
	return prettyFunction.substr(space, bracket - space);
}

#if defined(__GNUC__) || defined(__clang__)
	#define __METHOD_NAME__ methodName(__PRETTY_FUNCTION__)
#elif defined(_MSC_VER)
	#define __METHOD_NAME__ methodName(__FUNCSIG__)
#else
	#error "Compiler not supported"
#endif

namespace metrics {
	using Meter = opentelemetry::nostd::shared_ptr<metrics_api::Meter>;

	template <typename T>
	using Histogram = opentelemetry::nostd::unique_ptr<metrics_api::Histogram<T>>;

	template <typename T>
	using Counter = opentelemetry::nostd::unique_ptr<metrics_api::Counter<T>>;

	template <typename T>
	using UpDownCounter = opentelemetry::nostd::unique_ptr<metrics_api::UpDownCounter<T>>;

	struct Options {
		bool enablePrometheusExporter;
		bool enableOStreamExporter;

		metrics_sdk::PeriodicExportingMetricReaderOptions ostreamOptions;
		metrics_exporter::PrometheusExporterOptions prometheusOptions;
	};

	class ScopedLatency {
	public:
		explicit ScopedLatency(std::string_view name, const std::string &histogramName, const std::string &scopeKey);
		explicit ScopedLatency(std::string_view name, Histogram<double> &histogram, std::map<std::string, std::string> attrs = {}, opentelemetry::context::Context context = opentelemetry::context::Context()) :
			begin(std::chrono::steady_clock::now()), histogram(histogram), attrs(attrs), context(context) {
		}

		void stop();

		~ScopedLatency();

	private:
		opentelemetry::context::Context context;
		Histogram<double> &histogram;
		std::chrono::steady_clock::time_point begin;
		std::map<std::string, std::string> attrs;
		bool stopped { false };
	};

#define DEFINE_LATENCY_CLASS(class_name, histogram_name, category)       \
	class class_name##_latency final : public ScopedLatency {            \
	public:                                                              \
		class_name##_latency(std::string_view name) :                    \
			ScopedLatency(name, histogram_name "_latency", category) { } \
	}

	DEFINE_LATENCY_CLASS(method, "method", "method");
	DEFINE_LATENCY_CLASS(lua, "lua", "scope");
	DEFINE_LATENCY_CLASS(query, "query", "truncated_query");
	DEFINE_LATENCY_CLASS(task, "task", "task");
	DEFINE_LATENCY_CLASS(lock, "lock", "scope");

	const std::vector<std::string> latencyNames {
		"method_latency",
		"lua_latency",
		"query_latency",
		"task_latency",
		"lock_latency",
	};

	class Metrics final {
	public:
		Metrics() { }
		~Metrics() = default;

		void init(Options opts);
		void initHistograms();
		void shutdown();

		static Metrics &getInstance();

		void addCounter(std::string_view name, double value, std::map<std::string, std::string> attrs = {}) {
			std::scoped_lock lock(mutex_);
			if (!getMeter()) {
				return;
			}
			if (counters.find(name) == counters.end()) {
				std::string nameStr(name);
				counters[name] = getMeter()->CreateDoubleCounter(nameStr);
			}
			auto attrskv = opentelemetry::common::KeyValueIterableView<decltype(attrs)> { attrs };
			counters[name]->Add(value, attrskv);
		}

		void addUpDownCounter(std::string_view name, int value, std::map<std::string, std::string> attrs = {}) {
			std::scoped_lock lock(mutex_);
			if (!getMeter()) {
				return;
			}
			if (upDownCounters.find(name) == upDownCounters.end()) {
				std::string nameStr(name);
				upDownCounters[name] = getMeter()->CreateInt64UpDownCounter(nameStr);
			}
			auto attrskv = opentelemetry::common::KeyValueIterableView<decltype(attrs)> { attrs };
			upDownCounters[name]->Add(value, attrskv);
		}

		friend class ScopedLatency;

	protected:
		opentelemetry::context::Context defaultContext {};
		phmap::parallel_flat_hash_map<std::string, Histogram<double>> latencyHistograms;
		phmap::flat_hash_map<std::string, UpDownCounter<int64_t>> upDownCounters;
		phmap::flat_hash_map<std::string, Counter<double>> counters;

		Meter getMeter() {
			auto provider = metrics_api::Provider::GetMeterProvider();
			if (provider == nullptr) {
				return {};
			}
			return provider->GetMeter(meterName, otelVersion);
		}

	private:
		std::mutex mutex_;

		std::string meterName { "stats" };
		std::string otelVersion { "1.2.0" };
		std::string otelSchema { "https://opentelemetry.io/schemas/1.2.0" };
	};
}

constexpr auto g_metrics
	= metrics::Metrics::getInstance;
