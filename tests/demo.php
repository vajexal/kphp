<?php

function demo($features_path, $indices_to_features_path, $results_path) {
  $ads_features_json = file_get_contents($features_path);
  $ads_features = json_decode($ads_features_json, true);
  $indices_to_features_json = file_get_contents($indices_to_features_path);
  $indices_to_features = json_decode($indices_to_features_json, true);

  $feature_to_index = [];
  foreach($indices_to_features as $ind => $feature) {
    $feature_to_index[$feature] = $ind;
  }


  $prepared_features = [];
  foreach ($ads_features as $ad_id => $features) {
    $mapped_features = [];
    foreach ($features as $feature_name => $feature_value) {
      $feature_index = $feature_to_index[$feature_name];
      if ($feature_index) {
        $mapped_features[$feature_index] = (float)$feature_value;
      }
    }

    $prepared_features[$ad_id] = $mapped_features;
  }

  $measurements = [];

  $count = 100;
  for ($i = 0; $i < $count; ++$i) {
    $start = hrtime(true);

    $results = xgboost_predict($prepared_features);

    $elapsed = (hrtime(true) - $start) / 1000000000;
    echo "elapsed: ", $elapsed, ", s\n";
    $measurements[] = $elapsed;
    file_put_contents($results_path, json_encode($results));
  }

  $total = 0;
  foreach ($measurements as $m) {
    $total += $m;
  }
  echo "\naverage: ", $total / $count, ", s\n";
}

demo("/home/slava/vk/all_ml_from_tar_gz/data/features_per_ad.json",
     "/home/slava/vk/all_ml_from_tar_gz/data2/indices_to_features.json",
     "/home/slava/vk/kphp/tests/output_kphp.json");
