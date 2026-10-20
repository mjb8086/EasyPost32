`postman-v2.1.0.json` is the unmodified official Postman Collection v2.1.0
JSON Schema, downloaded from
https://schema.postman.com/json/collection/v2.1.0/collection.json on 2026-10-20.
Its canonical identifier is https://schema.getpostman.com/json/collection/v2.1.0/.

The native tests emit actual production exports into the runner's private
temporary directory. `validate_postman.py` validates these using Draft 4 and a
registry that rejects external retrieval. Test execution needs no internet.
