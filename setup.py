from distutils.command.install import install
from distutils.command.build import build

from setuptools import Command


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


build.sub_commands.append(("build_hook", lambda x: True))
install.sub_commands.append(("install_hook", lambda x: True))


############################################


import os
import re
import shutil
import sys
import platform
import subprocess

from setuptools import setup, Extension
from setuptools.command.build_ext import build_ext
from distutils.version import LooseVersion


class CMakeExtension(Extension):
    def __init__(self, name, sourcedir=''):
        Extension.__init__(self, name, sources=['./'])
        self.sourcedir = os.path.abspath(sourcedir)



def copy_vdf_client(build_dir, install_dir):
    shutil.copy("vdf_client", install_dir)


def invoke_make(**kwargs):
    subprocess.check_output('make -f Makefile.vdf-client', shell=True)


if os.getenv("BUILD_VDF_CLIENT", "Y") == "Y":
    add_install_hook(copy_vdf_client)
    add_build_hook(invoke_make)


class CMakeBuild(build_ext):
    def run(self):
        try:
            out = subprocess.check_output(['cmake', '--version'])
        except OSError:
            raise RuntimeError("CMake must be installed to build" +
                               " the following extensions: " +
                               ", ".join(e.name for e in self.extensions))

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
                self.distribution.get_version())
        subprocess.check_call(['cmake', ext.sourcedir] + cmake_args, env=env)
        subprocess.check_call(['cmake', '--build', '.'] + build_args)


setup(
    name='chiavdf',
    author='Florin Chirica',
    author_email='florin@chia.net',
    description='Chia vdf verification (wraps C++)',
    license='Apache License',
    python_requires='>=3.5',
    long_description=open('README.md').read(),
    ext_modules=[CMakeExtension('chiavdf', '.')],
    cmdclass=dict(build_ext=CMakeBuild, install_hook=install_hook, build_hook=build_hook),
    zip_safe=False,
)
