import os
import re
import shutil
import sys
import platform
import subprocess

from distutils.command.build import build
from distutils.command.install import install
from distutils.version import LooseVersion
from setuptools.command.build_ext import build_ext
from setuptools import setup, setuptools, Extension, Command


BUILD_HOOKS = []
INSTALL_HOOKS = []


def add_install_hook(hook):
    INSTALL_HOOKS.append(hook)


def add_build_hook(hook):
    BUILD_HOOKS.append(hook)


class HookCommand(Command):
    def __init__(self, dist):
        self.dist = dist
        Command.__init__(self, dist)

    def initialize_options(self, *args):
        self.install_dir = None
        self.build_dir = None

    def finalize_options(self):
        self.set_undefined_options('build', ('build_scripts', 'build_dir'))
        self.set_undefined_options('install',
                                   ('install_platlib', 'install_dir'),
                                   )

    def run(self):
        for _ in self.hooks:
            _(install_dir=self.install_dir, build_dir=self.build_dir)


class build_hook(HookCommand):
    hooks = BUILD_HOOKS


class install_hook(HookCommand):
    hooks = INSTALL_HOOKS

############################################


class CMakeExtension(Extension):
    def __init__(self, name, sourcedir=''):
        Extension.__init__(self, name, sources=['./'])
        self.sourcedir = os.path.abspath(sourcedir)


def copy_vdf_client(build_dir, install_dir):
    shutil.copy("src/vdf_client", install_dir)
    shutil.copy("src/prover_test", install_dir)
    shutil.copy("src/1weso_test", install_dir)
    shutil.copy("src/2weso_test", install_dir)


def copy_vdf_bench(build_dir, install_dir):
    shutil.copy("src/vdf_bench", install_dir)


def invoke_make(**kwargs):
    subprocess.check_output('make -C src -f Makefile.vdf-client', shell=True)


BUILD_VDF_CLIENT = (os.getenv("BUILD_VDF_CLIENT", "Y") == "Y")
BUILD_VDF_BENCH = (os.getenv("BUILD_VDF_BENCH", "N") == "Y")


if BUILD_VDF_CLIENT or BUILD_VDF_BENCH:
    add_build_hook(invoke_make)

if BUILD_VDF_CLIENT:
    add_install_hook(copy_vdf_client)

if BUILD_VDF_BENCH:
    add_install_hook(copy_vdf_bench)


class CMakeBuild(build_ext):
    def run(self):
        try:
            out = subprocess.check_output(['cmake', '--version'])
        except OSError:
            raise RuntimeError("CMake must be installed to build"
                               + " the following extensions: "
                               + ", ".join(e.name for e in self.extensions))

        if platform.system() == "Windows":
            cmake_version = LooseVersion(
                re.search(r'version\s*([\d.]+)', out.decode()).group(1))
            if cmake_version < '3.1.0':
                raise RuntimeError("CMake >= 3.1.0 is required on Windows")

        for ext in self.extensions:
            self.build_extension(ext)

    def build_extension(self, ext):
        extdir = os.path.abspath(os.path.dirname(self.get_ext_fullpath(ext.name)))
        cmake_args = ['-DCMAKE_LIBRARY_OUTPUT_DIRECTORY=' + str(extdir),
                      '-DPYTHON_EXECUTABLE=' + sys.executable]

        cfg = 'Debug' if self.debug else 'Release'
        build_args = ['--config', cfg]

        if platform.system() == "Windows":
            cmake_args += ['-DCMAKE_LIBRARY_OUTPUT_DIRECTORY_{}={}'
                           .format(cfg.upper(), extdir)]
            if sys.maxsize > 2**32:
                cmake_args += ['-A', 'x64']
            build_args += ['--', '/m']
        else:
            cmake_args += ['-DCMAKE_BUILD_TYPE=' + cfg]
            build_args += ['--', '-j', '6']

        env = os.environ.copy()
        env['CXXFLAGS'] = '{} -DVERSION_INFO=\\"{}\\"'.format(
            env.get('CXXFLAGS', ''),
            self.distribution.get_version()
        )
        subprocess.check_call(['cmake', ext.sourcedir] + cmake_args, env=env)
        subprocess.check_call(['cmake', '--build', '.'] + build_args)


class get_pybind_include(object):
    """Helper class to determine the pybind11 include path

    The purpose of this class is to postpone importing pybind11
    until it is actually installed, so that the ``get_include()``
    method can be invoked. """

    def __init__(self, user=False):
        self.user = user

    def __str__(self):
        import pybind11
        return pybind11.get_include(self.user)


ext_modules = [
    Extension(
        'chiavdf',
        sorted(
            [
                "src/python_bindings/fastvdf.cpp",
                "src/refcode/lzcnt.c",
            ]
        ),
        include_dirs=[
            # Path to pybind11 headers
            get_pybind_include(),
            get_pybind_include(user=True),
            'mpir_gc_x64',
        ],
        library_dirs=['mpir_gc_x64'],
        libraries=['mpir'],
        language='c++'
    ),
]


# As of Python 3.6, CCompiler has a `has_flag` method.
# cf http://bugs.python.org/issue26689
def has_flag(compiler, flagname):
    """Return a boolean indicating whether a flag name is supported on
    the specified compiler.
    """
    import tempfile
    with tempfile.NamedTemporaryFile('w', suffix='.cpp') as f:
        f.write('int main (int argc, char **argv) { return 0; }')
        try:
            compiler.compile([f.name], extra_postargs=[flagname])
        except setuptools.distutils.errors.CompileError:
            return False
    return True


def cpp_flag(compiler):
    """Return the -std=c++[11/14/17] compiler flag.

    The newer version is prefered over c++11 (when it is available).
    """
    flags = ['-std=c++17', '-std=c++14', '-std=c++11']

    for flag in flags:
        if has_flag(compiler, flag):
            return flag

    raise RuntimeError('Unsupported compiler -- at least C++11 support '
                       'is needed!')


class BuildExt(build_ext):
    """A custom build extension for adding compiler-specific options."""
    c_opts = {
        'msvc': ['/EHsc', '/std:c++17'],
        'unix': [""],
    }
    l_opts = {
        'msvc': [""],
        'unix': [""],
    }

    if sys.platform == 'darwin':
        darwin_opts = ['-stdlib=libc++', '-mmacosx-version-min=10.14']
        c_opts['unix'] += darwin_opts
        l_opts['unix'] += darwin_opts

    def build_extensions(self):
        ct = self.compiler.compiler_type
        opts = self.c_opts.get(ct, [])
        link_opts = self.l_opts.get(ct, [])
        if ct == 'unix':
            opts.append('-DVERSION_INFO="%s"' % self.distribution.get_version())
            opts.append(cpp_flag(self.compiler))
            if has_flag(self.compiler, '-fvisibility=hidden'):
                opts.append('-fvisibility=hidden')
        elif ct == 'msvc':
            opts.append('/DVERSION_INFO=\\"%s\\"' % self.distribution.get_version())
        for ext in self.extensions:
            ext.extra_compile_args = opts
            ext.extra_link_args = link_opts
        build_ext.build_extensions(self)


if platform.system() == "Windows":
    setup(
        name='chiavdf',
        author='Mariano Sorgente',
        author_email='mariano@chia.net',
        description='Chia vdf verification (wraps C++)',
        license='Apache License',
        python_requires='>=3.7',
        long_description=open('README.md').read(),
        long_description_content_type="text/markdown",
        build_requires=["pybind11"],
        url="https://github.com/Chia-Network/chiavdf",
        ext_modules=ext_modules,
        cmdclass={'build_ext': BuildExt},
        zip_safe=False,
    )
else:
    build.sub_commands.append(("build_hook", lambda x: True))
    install.sub_commands.append(("install_hook", lambda x: True))

    setup(
        name='chiavdf',
        author='Florin Chirica',
        author_email='florin@chia.net',
        description='Chia vdf verification (wraps C++)',
        license='Apache License',
        python_requires='>=3.7',
        long_description=open('README.md').read(),
        long_description_content_type="text/markdown",
        url="https://github.com/Chia-Network/chiavdf",
        setup_requires=['pybind11>=2.5.0'],
        ext_modules=[CMakeExtension('chiavdf', 'src')],
        cmdclass=dict(build_ext=CMakeBuild, install_hook=install_hook, build_hook=build_hook),
        zip_safe=False,
    )
