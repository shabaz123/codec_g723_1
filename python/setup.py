from setuptools import setup, find_packages

setup(
    name="codec_g723_1",
    version="0.1.0",
    packages=find_packages(),
    package_data={"codec_g723_1": ["*.dll", "*.so", "*.dylib"]},
)
