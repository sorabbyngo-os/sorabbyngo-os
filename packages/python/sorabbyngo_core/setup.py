from setuptools import setup, find_packages

setup(
    name="sorabbyngo-core",
    version="0.1.0",
    description="Sorabbyngo internal Python package — shared primitives for all services",
    author="Sorabbyngo Labs",
    packages=find_packages(),
    python_requires=">=3.10",
    install_requires=[
        "pyyaml>=6.0",
        "nats-py>=2.0",
    ],
    extras_require={
        "dev": ["pytest", "pytest-asyncio"],
    },
)
