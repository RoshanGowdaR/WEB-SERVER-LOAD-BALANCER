# Configuration Reference

The load balancer loads its configuration from a JSON file (specified via `-c <filepath>` or default `config/config.example.json`).

| Field | Type | Default | Description |
| :--- | :--- | :--- | :--- |
| **`host`** | string | `"127.0.0.1"` | IP address on which the load balancer binds and listens. |
| **`port`** | integer | `8080` | Port on which the load balancer accepts incoming HTTP connections. |
| **`worker_threads`** | integer | `8` | Number of worker threads allocated to the `ThreadPool`. |
| **`max_queue_size`** | integer | `2048` | Maximum number of tasks queued before backpressure rejection triggers. |
| **`connection_timeout_ms`** | integer | `5000` | Inactivity timeout on client sockets in milliseconds. |
| **`backend_timeout_ms`** | integer | `5000` | Connection and response read timeout when communicating with upstreams. |
| **`health_check_interval_ms`**| integer | `5000` | Frequency of periodic background upstream health checks in milliseconds. |
| **`max_header_size`** | integer | `8192` | Maximum allowable HTTP request header block in bytes. |
| **`max_body_size`** | integer | `10485760` | Maximum allowable HTTP request body in bytes (10 MB). |
| **`algorithm`** | string | `"round_robin"` | Load balancing strategy: `"round_robin"` or `"least_connections"`. |
| **`backends`** | array | - | Array of upstream objects: `[{"host":"127.0.0.1", "port":9001, "weight":1}]`. |
