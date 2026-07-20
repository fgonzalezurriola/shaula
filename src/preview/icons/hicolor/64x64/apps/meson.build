custom_target(
  'shaula-app-icon-' + shaula_icon_size,
  input: shaula_app_icon,
  output: 'shaula.png',
  command: [
    rsvg_convert,
    '--width=' + shaula_icon_size,
    '--height=' + shaula_icon_size,
    '--output=@OUTPUT@',
    '@INPUT@',
  ],
  build_by_default: true,
  install: true,
  install_dir: get_option('datadir') / 'icons' / 'hicolor' / shaula_icon_size + 'x' + shaula_icon_size / 'apps',
)
