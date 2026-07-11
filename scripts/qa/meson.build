qa_bash_program = find_program('bash')

shaula_fake_runtime_capture_helper = executable(
  'shaula-fake-runtime-capture-helper',
  'fake_runtime_capture_helper.c',
  dependencies: gdk_pixbuf_dep,
  install: false,
)

test(
  'port-command-compatibility',
  qa_bash_program,
  args: [
    files('assert-port-command-compatibility.sh'),
    shaula_exe,
    shaula_fake_runtime_capture_helper,
  ],
  suite: ['port', 'contract'],
  timeout: 60,
)
