project('syna_00dc_tod', 'c',
    version: '0.1.0',
    default_options: [
        'c_std=gnu11',
        'warning_level=2',
        'buildtype=debugoptimized'
    ]
)

cc = meson.get_compiler('c')
m_dep = cc.find_library('m', required: false)

libfprint_tod_dep = dependency('libfprint-2-tod-1', required: true)
libfprint_public_dep = dependency('libfprint-2', required: true)
openssl_dep = dependency('openssl', version: '>= 3.0', required: true)
gusb_dep = dependency('gusb', required: true)
libusb_dep = dependency('libusb-1.0', required: true)
glib_dep = dependency('glib-2.0', required: true)
gio_dep = dependency('gio-2.0', required: true)

tod_sources = [
    'src/syna_device.c',
    'src/syna_ssm.c',
    'src/syna_template.c',
    'src/syna_mode2.c',
    'src/syna_matcher.c',
    'src/syna_tls.c',
    'src/syna_oem.c'
]

driver_includes = include_directories('src')

tod_driver = shared_module('syna_00dc_tod',
    sources: tod_sources,
    c_args: [
        '-D_GNU_SOURCE',
        '-Wno-deprecated-declarations',
        '-Wno-missing-field-initializers'
    ],
    include_directories: driver_includes,
    dependencies: [
        libfprint_tod_dep,
        openssl_dep,
        gusb_dep,
        libusb_dep,
        glib_dep,
        gio_dep,
        m_dep
    ],
    install: true,
    install_dir: libfprint_tod_dep.get_variable(pkgconfig: 'tod_driversdir')
)

# Unit test: Mode-2 decoder
test_mode2_bin = executable('test_mode2',
    sources: ['tests/test_mode2.c', 'src/syna_mode2.c'],
    include_directories: driver_includes
)
test('mode2_tests', test_mode2_bin)

# Unit test: Matcher
test_matcher_bin = executable('test_matcher',
    sources: ['tests/test_matcher.c', 'src/syna_matcher.c'],
    include_directories: driver_includes,
    dependencies: [m_dep]
)
test('matcher_tests', test_matcher_bin)

# Unit test: TLS crypto
test_tls_bin = executable('test_tls',
    sources: ['tests/test_tls.c', 'src/syna_tls.c'],
    include_directories: driver_includes,
    c_args: ['-Wno-deprecated-declarations'],
    dependencies: [openssl_dep]
)
test('tls_tests', test_tls_bin)

# Unit test: OEM loader
test_oem_bin = executable('test_oem',
    sources: ['tests/test_oem.c', 'src/syna_oem.c'],
    include_directories: driver_includes
)
test('oem_tests', test_oem_bin)

# Diagnostic test: FpContext device enumeration
test_context_bin = executable('test_fprint_context',
    sources: ['tests/test_fprint_context.c'],
    c_args: ['-Wno-unused-parameter'],
    dependencies: [libfprint_public_dep, glib_dep]
)

# Diagnostic test: FpDevice open and close
test_open_bin = executable('test_fprint_open',
    sources: ['tests/test_fprint_open.c'],
    c_args: ['-Wno-unused-parameter'],
    dependencies: [libfprint_public_dep, glib_dep]
)

# Interactive test: Enrollment and Verification
test_enroll_verify_bin = executable('test_fprint_enroll_verify',
    sources: ['tests/test_fprint_enroll_verify.c'],
    c_args: ['-Wno-unused-parameter'],
    dependencies: [libfprint_public_dep, glib_dep]
)


test_template_bin = executable('test_template',
    sources: ['tests/test_template.c', 'src/syna_template.c'],
    include_directories: driver_includes,
    dependencies: [glib_dep, m_dep])
test('template_tests', test_template_bin)

test_timing_bin = executable('test_timing', sources: 'tests/test_timing.c',
    include_directories: driver_includes, dependencies: glib_dep)
test('timing_tests', test_timing_bin)

test_status_bin = executable('test_status', sources: 'tests/test_status.c',
    include_directories: driver_includes)
test('status_tests', test_status_bin)

test_pipeline_bin = executable('test_pipeline',
    sources: ['tests/test_pipeline.c', 'src/syna_matcher.c'],
    include_directories: driver_includes,
    dependencies: [m_dep])
test('pipeline_tests', test_pipeline_bin)

test_burst_bin = executable('test_burst',
    sources: ['tests/test_burst_frames.c', 'src/syna_mode2.c', 'src/syna_matcher.c'],
    include_directories: driver_includes,
    dependencies: [m_dep])
test('burst_tests', test_burst_bin)

test_real_scores_bin = executable('test_real_scores',
    sources: ['tests/test_real_scores.c', 'src/syna_mode2.c', 'src/syna_matcher.c'],
    include_directories: driver_includes,
    dependencies: [m_dep])
test('real_scores_tests', test_real_scores_bin)

