"""Validate production exports against the official, checked-in Postman schema offline."""
import json
from pathlib import Path

from jsonschema import Draft4Validator
from referencing import Registry, Resource
from referencing.exceptions import NoSuchResource


def reject_external(uri):
    # Even validator references must never reach the network during test runs.
    raise NoSuchResource(ref=uri)


def validate_exports(directory):
    schema = json.loads((Path(__file__).parent / "Schemas/postman-v2.1.0.json").read_text(encoding="utf-8"))
    registry = Registry(retrieve=reject_external).with_resource(schema["id"], Resource.from_contents(schema))
    validator = Draft4Validator(schema, registry=registry)
    exports = sorted(directory.glob("*.postman.json"))
    if not exports:
        raise RuntimeError("No production Postman exports were produced for schema validation")
    for path in exports:
        value = json.loads(path.read_text(encoding="utf-8"))
        # Keep validation diagnostics free of request values, bodies and credentials.
        if not validator.is_valid(value):
            raise RuntimeError("A production Postman export failed official v2.1 schema validation")
    print(f"{len(exports)} production Postman exports passed the official v2.1 schema (offline)", flush=True)
