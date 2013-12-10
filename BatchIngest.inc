<?php

/**
 * Batch creation form submit.
 * @global user $user
 *
 * @param array $form_state
 * @param array $content_models
 */
function batch_creation_form(&$form_state, $collection_pid) {
  module_load_include('inc', 'fedora_repository', 'api/fedora_utils');
  module_load_include('inc', 'fedora_repository', 'CollectionPolicy');
  $policy = CollectionPolicy::loadFromCollection($collection_pid, TRUE);
  if (!$policy) {
    $form['titlebox'] = array(
      '#type' => 'item',
      '#value' => t("This collection is missing a Collection Policy"),
    );

    return $form;
  }

  $content_models = $policy->getContentModels();

  $cm_options = array();
  $name_mappings = array();
  foreach ($content_models as $content_model) {
    if ($content_model->pid != "islandora:collectionCModel") {
      $cm_options[$content_model->pid] = $content_model->name;
      $name_mappings[] = $content_model->pid . '^' . $content_model->pid_namespace;
    }
  }

  $mappings = implode('~~~', $name_mappings);
  $form['#attributes']['enctype'] = 'multipart/form-data';
  $form['link'] = array(
    '#type' => 'item',
    '#value' => l(t("Return to collection view"), "fedora/repository/$collection_pid"),
  );
  $form['titlebox'] = array(
    '#type' => 'item',
    '#value' => t("Batch ingest into $collection_pid"),
  );

  $form['collection_pid'] = array(
    '#type' => 'hidden',
    '#value' => $collection_pid,
  );
  $form['namespace_mappings'] = array(
    '#type' => 'hidden',
    '#value' => $mappings,
  );
  $form['content_model'] = array(
    '#title' => "Choose content model to be associated with objects ingested",
    '#type' => 'select',
    '#options' => $cm_options,
    '#required' => TRUE,
    '#description' => t("Content models describe the behaviours of objects with which they are associated."),
  );
  $form['indicator']['file-location'] = array(
    '#type' => 'file',
    '#title' => t('Upload zipped folder'),
    '#size' => 48,
    '#description' => t('The zipped folder should contain all files necessary to build objects.<br .>
      Related files must have the same filename, but with differing extensions to indicate mimetypes.<br />
      ie. <em>myFile.xml</em> and <em>myFile.jpg</em>'),
  );

  $form['dsid_xml'] = array(
    '#title' => "Datastream ID for original XML ingested",
    '#type' => 'textfield',
    '#size' => 64,
    '#maxlength' => 64,
  );

  $form['transform'] = array(
    '#title' => "XML Datastream Transform",
    '#type' => 'fieldset',
    '#collapsible' => TRUE,
    '#collapsed' => TRUE,
  );
  $transforms[NULL] = t('Select a transform');
  $cmf_transforms = module_invoke_all('islandora_content_model_forms_get_transforms');
  foreach ($cmf_transforms as $key => $transform) {
    if (file_exists($key)) {
      $transforms[$key] = $transform;
    }
  }

  $moule_path = drupal_get_path('module', 'islandora_batch_ingest');
  $include_path = "$moule_path/xslt";
  $dir = opendir($include_path);
  if ($dir !== FALSE) {
    while (($file = readdir($dir)) !== FALSE) {
      if (preg_match('/\.xslt?$/', $file)) {
        $transforms["$include_path/$file"] = $file . " (islandora_batch_ingest)";
      }
    }
    closedir($dir);
  }

  $form['transform']['transform_xslt'] = array(
    '#title' => "Choose an XSLT",
    '#type' => 'select',
    '#options' => $transforms,
    '#description' => t("This XSLT will be used when transforming ingested XML"),
  );

  $form['transform']['dsid_xml_transform'] = array(
    '#title' => "Datastream ID for transformed XML",
    '#type' => 'textfield',
    '#size' => 64,
    '#maxlength' => 64,
  );

  $form['submit'] = array(
    '#type' => 'submit',
    '#value' => t('Create Objects'),
  );
  return ($form);
}

/**
 * Batch form validate
 *
 * @param array $form
 * @param array $form_state
 */
function batch_creation_form_validate($form, &$form_state) {

  $field_name = 'file-location';
  if (isset($_FILES['files']) && is_uploaded_file($_FILES['files']['tmp_name'][$field_name])) {
    $file = file_save_upload($field_name);
    if ($file->filemime != 'application/zip') {
      form_set_error($field_name, 'Input file must be a .zip file');
      return;
    }
    if (!$file) {
      form_set_error($field_name, 'Error uploading file.');
      return;
    }
    $form_state['values']['file'] = $file;
  }
  else {
    // Set error.
    form_set_error($field_name, 'Error uploading file.');
    return;
  }
}

function batch_creation_form_submit($form, &$form_state) {
  module_load_include('inc', 'fedora_repository', 'ContentModel');
  module_load_include('inc', 'fedora_repository', 'api/fedora_item');
  global $user;
  $namespace_mappings = array();
  $content_model = $form_state['values']['content_model'];
  $metadata = $form_state['values']['metadata_type'];
  $collection_pid = $form_state['values']['collection_pid'];
  $namespace_process = explode('~~~', $form_state['values']['namespace_mappings']);
  foreach ($namespace_process as $line) {
    $line_parts = explode('^', $line);
    $namespace_mappings[$line_parts[0]] = $line_parts[1];
  }
  $namespace = $namespace_mappings[$content_model];
  $namespace = preg_replace('/:.*/', '', $namespace);
  $dir_name = "temp" . $user->uid;
  $tmp_dir = file_directory_path() . '/' . $dir_name . '/';
  if (is_dir($tmp_dir)) {
    recursive_directory_delete($tmp_dir);
  }
  mkdir($tmp_dir);
  $file = $form_state['values']['file'];
  $file_name = $file->filepath;
  $file_list = array();
  $cmd_string = "unzip -q -o -d $tmp_dir \"$file_name\"";
  system($cmd_string, $ret_val);
  $dirs = array();
  $do_not_add = array('.', '..', '__MACOSX');
  array_push($dirs, $tmp_dir);
  $files = scandir($tmp_dir);
  foreach ($files as $file) {
    if (is_dir("$tmp_dir/$file") & !in_array($file, $do_not_add)) {
      array_push($dirs, $tmp_dir . $file);
    }
  }
  foreach ($dirs as $directory) {
    if ($inputs = opendir($directory)) {
      while (FALSE !== ($file_name = readdir($inputs))) {
        if (!in_array($file_name, $do_not_add) && is_dir($file_name) == FALSE) {
          $ext = strrchr($file_name, '.');
          $base = preg_replace("/$ext$/", '', $file_name);
          $ext = substr($ext, 1);
          if ($ext) {
            if ($directory[strlen($directory) - 1] != '/') {
              $directory .= '/';
            }
            $file_list[$base][$ext] = "$directory" . $file_name;
          }
        }
      }
      closedir($inputs);
    }
  }

  if (($cm = ContentModel::loadFromModel($content_model, 'ISLANDORACM')) === FALSE) {
    drupal_set_message("$content_model not found", "error");
    return;
  }
  $item = new Fedora_Item($collection_pid);
  $policy = NULL;
  if (array_key_exists('CHILD_SECURITY', $item->datastreams)) {
    $policy = $item->get_datastream_dissemination('CHILD_SECURITY');
  }
  $batch = array(
    'title' => 'Ingesting Objects',
    'operations' => array(),
    'file' => drupal_get_path('module', 'islandora_batch_ingest') . '/BatchIngest.inc',
  );

  foreach ($file_list as $label => $object_files) {
    $params = array(
      'label' => $label,
      'content_model' => $content_model,
      'object_files' => $object_files,
      'collection_pid' => $collection_pid,
      'namespace' => $namespace,
      'metadata' => $metadata,
      'policy' => $policy,
      'dsid_xml' => $form_state['values']['dsid_xml'],
      'transform_xslt' => $form_state['values']['transform_xslt'],
      'dsid_xml_transform' => $form_state['values']['dsid_xml_transform'],
    );
    $batch['operations'][] = array('create_batch_objects', array($params));
  }
  $batch['operations'][] = array('recursive_directory_delete', array($tmp_dir));
  batch_set($batch);
  batch_process("fedora/repository/$collection_pid");
}

/**
 * Creates the fedora object and added the datastreams to it.
 *
 * @param array $params
 */
function create_batch_objects($params = array()) {
  module_load_include('inc', 'fedora_repository', 'ContentModel');
  module_load_include('inc', 'fedora_repository', 'MimeClass');
  module_load_include('inc', 'fedora_repository', 'api/fedora_item');
  module_load_include('inc', 'fedora_repository', 'api/dublin_core');
  global $user;

  $label = $params['label'];
  $content_model = $params['content_model'];
  $object_files = $params['object_files'];
  $collection_pid = $params['collection_pid'];
  $namespace = $params['namespace'];
  $metadata = $params['metadata'];
  $policy = $params['policy'];
  $dsid_xml = $params['dsid_xml'];
  $transform_xslt = $params['transform_xslt'];
  $dsid_xml_transform = $params['dsid_xml_transform'];

  $cm = ContentModel::loadFromModel($content_model, 'ISLANDORACM');
  $allowed_mime_types = $cm->getMimetypes();
  $mime_helper = new MimeClass();
  $pid = fedora_item::get_next_PID_in_namespace($namespace);
  $owner = ($user->uid > 0) ? $user->name : 'anonymous';

  $item = Fedora_item::ingest_new_item($pid, 'A', $label, $owner);
  if ($item) {
    drupal_set_message(t("Successfully added base object $pid"), 'status');
  }
  $item->add_relationship('hasModel', $content_model, FEDORA_MODEL_URI);
  $item->add_relationship('isMemberOfCollection', $collection_pid);
  if ($object_files['xml']) {
    try {
      $xml = file_get_contents($object_files['xml']);
      $dsid = ($dsid_xml ? $dsid_xml : 'XML');
      if (islandora_batch_ingest_add_xml_datastream($item, $dsid, $xml)) {
        drupal_set_message(t("Successfully added datastream $dsid to $pid"), 'status');
      }
      else {
        drupal_set_message(t("Could not add datastream $dsid to $pid"), 'error');
      }

      if (file_exists($transform_xslt)) {
        $dsid = ($dsid_xml_transform ? $dsid_xml_transform : 'TransformedXML');

        $proc = new XsltProcessor();
        $xsl = new DomDocument();
        $xsl->load($transform_xslt);
        $input = new DomDocument();
        $input->loadXML(trim($xml));
        $xsl = $proc->importStylesheet($xsl);
        $output = $proc->transformToDoc($input);
        $output = $output->saveXML();
        if ($output) {
          if (islandora_batch_ingest_add_xml_datastream($item, $dsid, $output)) {
            drupal_set_message(t("Successfully added datastream $dsid to $pid"), 'status');
          }
          else {
            drupal_set_message(t("Could not add datastream $dsid to $pid"), 'error');
          }
        }
      }

      if ($dc = $item->get_datastream_dissemination('DC')) {
        $dc = simplexml_load_string($dc);
        $titles = $dc->xpath('//dc:title');
        if (isset($titles[0])) {
          $item->modify_object($titles[0]);
        }
      }
      $item->forget();
    } catch (Exception $exc) {
      drupal_set_message(t("An error occured trying to add the XML datastreams to object $pid"), 'error');
    }


  }

  if ($policy) {
    $item->add_datastream_from_string($policy, 'POLICY');
  }
  unset($object_files['xml']);
  foreach ($object_files as $ext => $filename) {
    $file_mimetype = $mime_helper->get_mimetype($filename);
    if (in_array($file_mimetype, $allowed_mime_types)) {
      $filename = preg_replace('/\/\//', '/', $filename);
      $added = $cm->execIngestRules($filename, $file_mimetype);
    }
    else {
      $item->purge("$pid $label not ingested.  $file_mimetype not permitted in objects associated with $content_model");
      drupal_set_message("$pid $label not ingested.  $file_mimetype not permitted in objects associated with $content_model");
      continue;
    }
    $item->add_datastream_from_file($filename, "OBJ");

    if (!empty($_SESSION['fedora_ingest_files'])) {
      foreach ($_SESSION['fedora_ingest_files'] as $dsid => $datastream_file) {
        $parts = pathinfo($datastream_file);
        $item->add_datastream_from_file($datastream_file, $dsid, $parts['basename']);
      }
    }
  }
}

/**
 * Added on updates an xml datastream
 *
 * @param Fedora_Item $item
 * @param string $dsid
 * @param string $xml
 * @return mixed
 */
function islandora_batch_ingest_add_xml_datastream(Fedora_Item $item, $dsid = 'XML', $xml = '') {
  $datastreams = $item->get_datastreams_list_as_array();
  if (isset($datastreams[$dsid])) {
    $method = 'modify_datastream_by_value';
  }
  else {
    $method = 'add_datastream_from_string';
  }
  return $item->{$method}($xml, $dsid, $dsid, 'text/xml', 'X');
}

/**
 *
 * @param <string> $dir
 * @return <boolean>
 */
function recursive_directory_delete($dir) {
  if (!file_exists($dir))
    return TRUE;
  if (!is_dir($dir))
    return unlink($dir);
  foreach (scandir($dir) as $item) {
    if ($item == '.' || $item == '..')
      continue;
    if (!recursive_directory_delete($dir . DIRECTORY_SEPARATOR . $item))
      return FALSE;
  }
  return rmdir($dir);
}
