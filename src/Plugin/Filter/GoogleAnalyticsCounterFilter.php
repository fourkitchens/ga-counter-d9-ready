<?php

namespace Drupal\google_analytics_counter\Plugin\Filter;

use Drupal\Core\Plugin\ContainerFactoryPluginInterface;
use Drupal\Core\Path\CurrentPathStack;
use Drupal\Component\Utility\SafeMarkup;
use Drupal\filter\FilterProcessResult;
use Drupal\filter\Plugin\FilterBase;
use Drupal\google_analytics_counter\GoogleAnalyticsCounterManagerInterface;
use Symfony\Component\DependencyInjection\ContainerInterface;

/**
 * Add filter to show google analytics counter number.
 *
 * @Filter(
 *   id = "google_analytics_counter_filter",
 *   title = @Translation("Google Analytics Counter token"),
 *   description = @Translation("Adds a Google Analytics Counter token which prints the pageview count. Syntaxes: [gac|path/to/page], [gac|node/1234] [gac|1234], or [gac]."),
 *   type = Drupal\filter\Plugin\FilterInterface::TYPE_MARKUP_LANGUAGE,
 * )
 */
class GoogleAnalyticsCounterFilter extends FilterBase implements ContainerFactoryPluginInterface {

  /**
   * The current path.
   *
   * @var \Drupal\Core\Path\CurrentPathStack
   */
  protected $currentPath;

  /**
   * Drupal\google_analytics_counter\GoogleAnalyticsCounterCommon definition.
   *
   * @var \Drupal\google_analytics_counter\GoogleAnalyticsCounterManagerInterface
   */
  protected $manager;

  /**
   * Constructs a new SiteMaintenanceModeForm.
   *
   * @param array $configuration
   *   A configuration array containing information about the plugin instance.
   * @param string $plugin_id
   *   The plugin ID for the plugin instance.
   * @param mixed $plugin_definition
   *   The plugin implementation definition.
   * @param \Drupal\Core\Path\CurrentPathStack $current_path
   *   The current path.
   * @param \Drupal\google_analytics_counter\GoogleAnalyticsCounterManagerInterface $manager
   *   Google Analytics Counter Manager object.
   */
  public function __construct(array $configuration, $plugin_id, $plugin_definition, CurrentPathStack $current_path, GoogleAnalyticsCounterManagerInterface $manager) {
    parent::__construct($configuration, $plugin_id, $plugin_definition);

    $this->currentPath = $current_path;
    $this->manager = $manager;
  }

  /**
   * {@inheritdoc}
   */
  public static function create(ContainerInterface $container, array $configuration, $plugin_id, $plugin_definition) {
    return new static(
      $configuration,
      $plugin_id,
      $plugin_definition,
      $container->get('path.current'),
      $container->get('google_analytics_counter.manager')
    );
  }

  /**
   * {@inheritdoc}
   */
  public function process($text, $langcode) {
    $text = $this->handleText($text);
    return new FilterProcessResult($text);
  }

  /**
   * Finds [gac|path/to/page] tags and replaces them by actual values.
   *
   * @param string $text
   *
   * @return mixed
   */
  private function handleText($text) {
    // [gac|path/to/page].
    $matchlink = '';
    $original_match = '';
    // This allows more than one pipe sign (|) ...
    // does not hurt and leaves room for possible extension.
    preg_match_all("/(\[)gac[^\]]*(\])/s", $text, $matches);

    foreach ($matches[0] as $match) {
      // Keep original value(s).
      $original_match[] = $match;

      // Display the page views. Page views includes page aliases, node/id,
      // and node/id/ URIs. If no path was defined, the function will detect
      // the current node's count.
      $matchlink[] = $this->manager->displayGaCount($this->currentPath->getPath());
    }

    return str_replace($original_match, $matchlink, $text);
  }

}
