test_c_args = [
  '-DTEST_DOMAIN_ONTOLOGY_RULE="@0@"'.format(tracker_uninstalled_domain_rule),
  '-DTEST_ONTOLOGIES_DIR="@0@"'.format(tracker_uninstalled_nepomuk_ontologies_dir),
]

steroids_test = executable('tracker-steroids-test',
  'tracker-test.c',
  dependencies: [tracker_common_dep, tracker_sparql_dep],
  c_args: [tracker_c_args, test_c_args])
test('steroids', steroids_test)
