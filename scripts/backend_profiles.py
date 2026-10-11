"""Canonical independent backend selections shared by validation tools."""

from itertools import combinations


BACKENDS = ("sqlite", "postgresql", "mysql", "mariadb")
PROFILES = ("core",) + tuple(
    "+".join(selected)
    for size in range(1, len(BACKENDS) + 1)
    for selected in combinations(BACKENDS, size)
)


def selected_backends(profile):
    if profile == "all":
        return BACKENDS
    if profile == "core":
        return ()
    selected = profile.split("+")
    if len(set(selected)) != len(selected) or any(value not in BACKENDS for value in selected):
        raise ValueError(f"Invalid backend selection: {profile}")
    return tuple(backend for backend in BACKENDS if backend in selected)
