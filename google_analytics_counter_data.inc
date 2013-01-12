<?php

/**
 * Find how many distinct paths does Google Analytics have for this profile. This function is triggered by hook_cron().
 */
function google_analytics_counter_update_path_counts(){
  /*
  $gapi->requestReportData($contact_center['report_id'], array('customVarValue1'), array('pageviews'), array('-pageviews'), NULL, $start_date, $end_date, 1, 1);    
  $count = $gapi->getTotalResults();
  $loops = ceil(($count/$max_results));
  */

  // Needing to stay under the Google Analytics API quota, let's count how many API retrievals were made in the last 24 hours.
  // @todo We should better take into consideration that the quota is reset at midnight PST (while time() always returns UTC).
  $timenow = time();
  $dayquota = variable_get('google_analytics_counter_dayquota', array(0, 0));
  if ($timenow - $dayquota[0] >= 86400) {
    // If last API request was more than a day ago, set monitoring time to now.
    $dayquota[0] = $timenow;
    $dayquota[1] = 0;
    variable_set('google_analytics_counter_dayquota', array($dayquota[0], $dayquota[1]));
  }
  //dpm($dayquota);
  // Are we over the GA API limit?
  $maxdailyrequests = variable_get('google_analytics_counter_api_dayquota', 10000); // see http://code.google.com/apis/analytics/docs/gdata/gdataDeveloperGuide.html#quota
  if ($dayquota[1] > $maxdailyrequests) {
    //dpm('limit hit!');
    watchdog('Google Analytics Counter', 'Google Analytics API quota of '.$maxdailyrequests.' requests has been reached. Will NOT fetch data from Google Analytics for the next '.($dayquota[0]+86400-$timenow).' seconds. See <a href="/admin/config/system/google_analytics_counter">the Google Analytics Counter settings page</a> for more info.', NULL, WATCHDOG_ERROR);
    return;
  }

  // How many results to ask from GA in one request. Default on 1000 to fit most systems (e.g. those with no external cron).
  $chunk = variable_get('google_analytics_counter_chunk_to_fetch', 1000);
  // In case there are more than $chunk path/counts to retrieve from GA, do just one chunk at a time and register that in $step.
  $step = variable_get('google_analytics_counter_data_step', 0);
  // Which GA result to look for first. Must be between 1 - infinity.
  $pointer = $step*$chunk+1; 
  //dpm('START chunk '.$chunk);
  //dpm('START step '.$step);
  //dpm('START pointer '.$pointer);

  /**
   * Request report data: google_analytics_api_report_data
   *
   * @param $params
   *   An associative array containing:
   *   - profile_id: required [default=variable_get('google_analytics_reports_profile_id')]
   *   - metrics: required.
   *   - dimensions: optional [default=none]
   *   - sort_metric: optional [default=none]
   *   - filters: optional [default=none]
   *   - segment: optional [default=none]
   *   - start_date: optional [default=GA release date]
   *   - end_date: optional [default=today]
   *   - start_index: optional [default=1]
   *   - max_results: optional [default=10,000]
   * @param $cache_options
   *   An optional associative array containing:
   *   - cid: optional [default=md5 hash]
   *   - expire: optional [default=CACHE_TEMPORARY]
   *   - refresh: optional [default=FALSE]
   */

  // The earliest valid start-date for Google Analytics is 2005-01-01.
  $request = array(
    'dimensions' => array('ga:pagePath'), // date would not be necessary for totals, but we also calculate stats of views per day, so we need it
    'metrics' => array('ga:pageviews'),
    'start_date' => strtotime('2005-01-01'),
      'end_date' => strtotime('tomorrow'), // Using 'tomorrow' to offset any timezone shift between the hosting and Google servers.
      'start_index' => $pointer,
      'max_results' => $chunk,
      //'filters' => 'ga:pagePath==/node/3',
      //'#start_date' => strtotime('2005-01-01'), // We want to retrieve all page views for this path. The earliest valid start-date for Google Analytics is 2005-01-01.
      //'sort_metric' => array('ga:date'),
      );

  $resultcount = FALSE;
  $cachehere = array(
    'cid' => 'google_analytics_counter_' . md5(serialize($request)),
      'expire' => google_analytics_reports_cache_time(), // Set at /admin/config/system/google-analytics-reports
      'refresh' => FALSE, //@todo
      );
  $new_data = @google_analytics_api_report_data($request, $cachehere);
  if(!empty($new_data->error)){ // If NULL then there is no error.
    watchdog('Google Analytics Counter', 'Problem fetching data from Google Analytics: ' . $new_data->error, NULL, WATCHDOG_ERROR);
  }
  else {
    $resultsretrieved = $new_data->results->rows;
    foreach($resultsretrieved as $val){

      // http://drupal.org/node/310085
      db_merge('google_analytics_counter')
      ->key(array('pagepath_hash' => md5($val['pagePath'])))
      ->fields(array(
        'pagepath' => $val['pagePath'],
        'pageviews' => $val['pageviews'],
        ))
      ->execute();

/*
      // Now, if it's a node, let's update also pageviews for it and all its known aliases. We will get the data from the local table already. We will write it into Drupal core table node_counter.
      // Get the normal node path if it is a node.
      $node_path = drupal_get_normal_path($val['pagePath']);
      // Let's get the nid, if there is one.
      // NOTE: Here $path DOES have an initial slash because it's coming from Google Analytics. 
      // E.g. /node/3 ... size = 3, second must be 'node', third must be a number.
      $nid = FALSE;
      $pathbits = explode('/', $node_path);
      if (sizeof($pathbits) == 3){
        if ((@$pathbits[0] == '' && @$pathbits[1] == 'node') && is_numeric(@$pathbits[2])) {
      // OK, it's in format node/x
          $nid = @$pathbits[2];
          dpm('ok it is a node '.$node_path.' with nid: '.$nid);
        }
      }
      dpm($node_path);
      dpm($pathbits);
      if ($nid !== FALSE) {
        // Get pageviews for this path and all its aliases.
        $pathwithnoinitialshlash = substr($val['pagePath'], 1);
        dpm('path: '.$pathwithnoinitialshlash);
        $unprefixedaliases = _google_analytics_counter_path_aliases($pathwithnoinitialshlash);
        dpm($unprefixedaliases);
        $allpaths = array();
        foreach($unprefixedaliases as $val){
          // Google Analytics stores initial slash as well, so let's prefix them.
          // Look for hashes of paths (primary key: faster).
          $allpaths[] = md5('/'.$val);
        }
        dpm($allpaths);
        // Get path counts for each of the path aliases.
        $pathcounts = db_select('google_analytics_counter', 'gac')
        //->fields('gac', array('pagepath', 'pageviews'))
        ->fields('gac', array('pageviews'))
        //->condition('pagepath', array('/bg', '/node/3'), 'IN')
        ->condition('pagepath_hash', $allpaths, 'IN')
        ->execute();
        $sum_of_pageviews = 0;
        foreach($pathcounts as $pathcount){
          dpm($pathcount);
          $sum_of_pageviews += $pathcount->pageviews;
        }
        dpm($sum_of_pageviews);
      }
      if ($sum_of_pageviews <> 0) {
      // This is a node, so update the node's counters.
      // We don't do anything with daycount here
      // Or we must create a new row to store counters for the new node.
      // http://drupal.org/node/310085
        db_merge('node_counter')
        ->key(array('nid' => $nid))
        ->fields(array(
          'daycount' => 0,
          'totalcount' => $sum_of_pageviews,
          'timestamp' => REQUEST_TIME,
          ))
        ->execute();
      }
*/

    }
  }
  //dpm($new_data);
  
  // The total number of records for this profile.
  $resultcount = $new_data->results->totalResults;
  //$resultcount = 4500; // @todo ARTIFICIAL BREAK TO REMOVE

  // The total number of hits for all records for this profile.
  $totalhits = $new_data->results->totalsForAllResults['pageviews']; 
  variable_set('google_analytics_counter_totalhits', $totalhits);
  //dpm('totalhits: '.$totalhits);

  // Set the pointer.
  $pointer += $chunk;

  // Increase the GA request limit tracker.
  variable_set('google_analytics_counter_dayquota', array($dayquota[0], ($dayquota[1]+1)));

  //dpm('step: '.$step.' | '.$pointer . ' out of total ' .$resultcount);
  watchdog('Google Analytics Counter', 'Retrieved '.sizeof($new_data->results->rows).' items from Google Analytics data for paths '.($pointer-$chunk).'-'.($pointer-$chunk-1+sizeof($new_data->results->rows)).'.', NULL, WATCHDOG_INFO);

  // OK now increase or zero $step
  if ($pointer < $resultcount){
    // If there are more results than what we've reached with this chunk, increase step to look further during the next run.
    $newstep = $step+1;
  }
  else {
    $newstep = 0;
  }
  //dpm('newstep: '.$newstep);
  variable_set('google_analytics_counter_data_step', $newstep);

}

/**
 * Calculate pageviews for one path (with any aliases).
 */
function google_analytics_counter_get_sum_per_path($path) {

  // Recognize special path 'all' to get the sum of all pageviews for the profile.
  if ($path == 'all') {
    //dpm('yep: '.variable_get('google_analytics_counter_totalhits', 0));
    return variable_get('google_analytics_counter_totalhits', 0);
  }

  $cacheid = md5($path);
  if ($cache = cache_get('google_analytics_counter_page_'.$cacheid)) {
    $sum_of_pageviews = $cache->data;
    dpm('CACHED');
  }
  else {
    // Do your expensive calculations here, and populate $my_data

    // Get pageviews for this path and all its aliases.
    // NOTE: Here $path does NOT have an initial slash because it's coming from either check_plain($_GET['q']) (block) or from a tag like [gac|node/N].
    //dpm('path: '.$path);
    $unprefixedaliases = _google_analytics_counter_path_aliases($path);
    //dpm($unprefixedaliases);
    $allpaths = array();
    foreach($unprefixedaliases as $val){
    // Google Analytics stores initial slash as well, so let's prefix them.
      $allpaths[] = md5('/'.$val);
    }
    //dpm($allpaths);

    // Get path counts for each of the path aliases.
    $pathcounts = db_select('google_analytics_counter', 'gac')
    //->fields('gac', array('pagepath', 'pageviews'))
    ->fields('gac', array('pageviews'))
    //->condition('pagepath', array('/bg', '/node/3'), 'IN')
    ->condition('pagepath_hash', $allpaths, 'IN')
    ->execute();
    $sum_of_pageviews = 0;
    foreach($pathcounts as $pathcount){
      //dpm($pathcount);
      $sum_of_pageviews += $pathcount->pageviews;
    }
    //dpm($sum_of_pageviews);

    // Get the normal node path if it is a node.
    $node_path = drupal_get_normal_path($path);
    // Let's get the nid
    $nid = FALSE;
    $pathbits = explode('/', $node_path);
    if (sizeof($pathbits) == 2){
      if ((@$pathbits[0] == 'node') && is_numeric(@$pathbits[1])) {
    // OK, it's in format node/x
        $nid = @$pathbits[1];
        //dpm('ok it is a node '.$node_path.' with nid: '.$nid);
      }
    }
    //dpm($path);
    //dpm($node_path);
    //dpm($pathbits);
    if ($nid !== FALSE AND $sum_of_pageviews <> 0) {
    // This is a node, so update the node's counters.
    // We don't do anything with daycount here
    // Or we must create a new row to store counters for the new node.
    // http://drupal.org/node/310085
      db_merge('node_counter')
      ->key(array('nid' => $nid))
      ->fields(array(
        'daycount' => 0,
        'totalcount' => $sum_of_pageviews,
        'timestamp' => REQUEST_TIME,
        ))
      ->execute();
    }

    cache_set('google_analytics_counter_page_'.$cacheid, $sum_of_pageviews, 'cache', CACHE_TEMPORARY);
    dpm('UNCACHED');
  }

  //dpm($sum_of_pageviews);
  return $sum_of_pageviews;
}

/*
 * Return a list of paths that are aliased with the given path (including the given path).
 */
function _google_analytics_counter_path_aliases($node_path) {

  // Get the normal node path if it is a node.
  $node_path = drupal_get_normal_path($node_path);
  //dpm($node_path);

  // Grab all aliases.
  $aliases = array($node_path);
  $result = db_query("SELECT * FROM {url_alias} WHERE source = :source", array(':source' => $node_path));
  foreach ($result as $row) {
    $aliases[] = $row->alias;
  }

  // If this is the front page, add the base path too, and index.php for good measure. There may be other ways that the user is accessing the front page but we can't account for them all.
  if ($node_path == drupal_get_normal_path(variable_get('site_frontpage', 'node'))) {
    $aliases[] = '';
    $aliases[] = 'index.php';
  }

  return $aliases;
}