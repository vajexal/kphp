<?php

require_once 'SharedImmutableMessageScenario/http_worker.php';
require_once 'utils.php';

function do_http_worker() {
  switch($_SERVER["PHP_SELF"]) {
    case "/test_simple_cpu_job": {
      test_simple_cpu_job();
      return;
    }
    case "/test_cpu_job_and_rpc_usage_between": {
      test_cpu_job_and_rpc_usage_between();
      return;
    }
    case "/test_cpu_job_and_mc_usage_between": {
      test_cpu_job_and_mc_usage_between();
      return;
    }
    case "/test_job_script_timeout_error": {
      test_job_script_timeout_error();
      return;
    }
    case "/test_job_errors": {
      test_job_errors();
      return;
    }
    case "/test_jobs_in_wait_queue": {
      test_jobs_in_wait_queue();
      return;
    }
    case "/test_complex_scenario": {
      require_once "ComplexScenario/_http_scenario.php";
      run_http_complex_scenario();
      return;
    }
    case "/test_client_too_big_request": {
      test_client_too_big_request();
      return;
    }
    case "/test_client_wait_false": {
      test_client_wait_false();
      return;
    }
    case "/test_job_graceful_shutdown": {
      test_job_graceful_shutdown();
      return;
    }
    case "/test_job_worker_wakeup_on_merged_events": {
      test_job_worker_wakeup_on_merged_events();
      return;
    }
    case "/test_job_worker_start_multi_with_shared_data": {
      test_job_worker_start_multi_with_shared_data();
      return;
    }
    case "/test_job_worker_start_multi_without_shared_data": {
      test_job_worker_start_multi_without_shared_data();
      return;
    }
    case "/test_error_different_shared_memory_pieces": {
      test_error_different_shared_memory_pieces();
      return;
    }
    case "/test_job_worker_start_multi_with_errors": {
      test_job_worker_start_multi_with_errors();
      return;
    }
    case "/test_send_job_no_reply": {
      test_send_job_no_reply();
      return;
    }
    case "/test_reference_invariant": {
      require_once "ReferenceInvariant/http_worker.php";
      test_reference_invariant();
      return;
    }
  }

  critical_error("unknown test " . $_SERVER["PHP_SELF"]);
}

function send_jobs($context, float $timeout = -1.0, bool $no_reply = false): array {
  $ids = [];
  foreach ($context["data"] as $arr) {
    $req = new X2Request;
    $req->tag = (string)$context["tag"];
    $req->redirect_chain_len = (int)$context["redirect-chain-len"];
    $req->master_port = (int)$context["master-port"];
    $req->arr_request = (array)$arr;
    $req->sleep_time_sec = (int)$context["job-sleep-time-sec"];
    $req->error_type = (string)$context["error-type"];
    if ($no_reply) {
      $ok = kphp_job_worker_start_no_reply($req, $timeout);
      if (!$ok) {
        critical_error("Can't send no_reply job");
      }
    } else {
      $id = kphp_job_worker_start($req, $timeout);
      if ($id) {
        $ids[] = $id;
      } else {
        critical_error("Can't send job");
      }
    }
  }
  return $ids;
}

function gather_jobs($ids): array {
  $result = [];
  foreach($ids as $id) {
    /**
     * @var KphpJobWorkerResponse
     */
    $resp = wait($id);
    if ($resp instanceof X2Response) {
      $result[] = ["data" => $resp->arr_reply, "stats" => $resp->stats];
    } else if ($resp instanceof KphpJobWorkerResponseError) {
      $result[] = ["error" => $resp->getError(), "error_code" => $resp->getErrorCode()];
    }
  }
  return $result;
}

function test_simple_cpu_job() {
  $context = json_decode(file_get_contents('php://input'));
  $ids = send_jobs($context);
  echo json_encode(["jobs-result" => gather_jobs($ids)]);
}

function test_cpu_job_and_rpc_usage_between() {
  $context = json_decode(file_get_contents('php://input'));
  $ids = send_jobs($context);

  $master_port_connection = new_rpc_connection("localhost", $context["master-port"]);
  $stat_id = rpc_tl_query_one($master_port_connection, ['_' => 'engine.stat']);
  $self_stats = rpc_tl_query_result_one($stat_id);

  echo json_encode(["stats" => $self_stats, "jobs-result" => gather_jobs($ids)]);
}

function test_cpu_job_and_mc_usage_between() {
  $context = json_decode(file_get_contents('php://input'));
  $ids = send_jobs($context);

  $mc = new McMemcache();
  $mc->addServer("localhost", (int)$context["master-port"]);
  $self_stats = $mc->get("stats");

  echo json_encode(["stats" => $self_stats, "jobs-result" => gather_jobs($ids)]);
}

function test_job_script_timeout_error() {
  $context = json_decode(file_get_contents('php://input'));
  $ids = send_jobs($context, (float)$context["script-timeout"]);
  echo json_encode(["jobs-result" => gather_jobs($ids)]);
}

function test_job_errors() {
  $context = json_decode(file_get_contents('php://input'));
  $ids = send_jobs($context, 15);
  echo json_encode(["jobs-result" => gather_jobs($ids)]);
}

function raise_error(string $err) {
  echo json_encode(["error" => $err]);
}

function test_client_too_big_request() {
  $req = new X2Request;
  $req->arr_request = make_big_fake_array();
  echo json_encode(["job-id" => kphp_job_worker_start($req, -1)]);
}

function test_jobs_in_wait_queue() {
  $context = json_decode(file_get_contents('php://input'));
  $ids = send_jobs($context);

  $wait_queue = wait_queue_create($ids);

  $id = wait_queue_next($wait_queue, 0.2);
  if ($id !== false) {
    raise_error("Too short wait time in wait_queue");
    return;
  }

  $job_sleep_time = (float)$context["job-sleep-time-sec"];
  $result = [];
  while (!wait_queue_empty($wait_queue)) {
    $ready_id = wait_queue_next($wait_queue, $job_sleep_time + 0.2);
    if ($ready_id === false) {
      raise_error("Too long wait time in wait_queue");
      return;
    }
    $result[$ready_id] = gather_jobs([$ready_id])[0];
  }
  ksort($result);

  echo json_encode(["jobs-result" => array_values($result)]);
}

function test_client_wait_false() {
  $id = false;
  if ($id) {
    $id = kphp_job_worker_start(new X2Request, -1);
  }
  $result = wait($id);
  echo json_encode(["jobs-result" => $result === null ? "null" : "not null"]);
}

function test_job_graceful_shutdown() {
  $context = json_decode(file_get_contents('php://input'));
  $ids = send_jobs($context);
  echo json_encode(["jobs-result" => gather_jobs($ids)]);
}

function test_job_worker_wakeup_on_merged_events() {
  [$waiting_workers, $sync_job_ids] = suspend_job_workers(get_job_workers_number());

  fwrite(STDERR, "Sync jobs started\n");

  $context = json_decode(file_get_contents('php://input'));
  $ids = send_jobs($context, 10);

  fwrite(STDERR, "Simple jobs were sent\n");

  resume_job_workers($waiting_workers, $sync_job_ids);

  echo json_encode(["jobs-result" => gather_jobs($ids)]);
}

function test_send_job_no_reply() {
  $context = json_decode(file_get_contents('php://input'));
  send_jobs($context, (int)$context["send-timeout"], true);
  echo json_encode("Success");
}
