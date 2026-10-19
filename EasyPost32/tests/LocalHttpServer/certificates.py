"""Fresh test-only PKI, with revocation information served on loopback."""
from datetime import datetime, timedelta, timezone
import ipaddress

from cryptography import x509
from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import rsa
from cryptography.x509.oid import ExtendedKeyUsageOID, NameOID


def generate(directory, http_port):
    now = datetime.now(timezone.utc)
    key = rsa.generate_private_key(public_exponent=65537, key_size=2048)
    name = x509.Name([x509.NameAttribute(NameOID.COMMON_NAME, "EasyPost32 ephemeral test CA")])
    point = x509.CRLDistributionPoints([x509.DistributionPoint(
        full_name=[x509.UniformResourceIdentifier(f"http://127.0.0.1:{http_port}/fixture-ca.crl")],
        relative_name=None, reasons=None, crl_issuer=None)])
    ca = (x509.CertificateBuilder().subject_name(name).issuer_name(name)
          .public_key(key.public_key()).serial_number(x509.random_serial_number())
          .not_valid_before(now - timedelta(days=1)).not_valid_after(now + timedelta(days=30))
          .add_extension(x509.BasicConstraints(ca=True, path_length=0), critical=True)
          .add_extension(x509.KeyUsage(digital_signature=True, content_commitment=False,
              key_encipherment=False, data_encipherment=False, key_agreement=False,
              key_cert_sign=True, crl_sign=True, encipher_only=None, decipher_only=None), critical=True)
          .add_extension(x509.SubjectKeyIdentifier.from_public_key(key.public_key()), critical=False)
          .add_extension(point, critical=False).sign(key, hashes.SHA256()))
    (directory / "ca.pem").write_bytes(ca.public_bytes(serialization.Encoding.PEM))
    crl = (x509.CertificateRevocationListBuilder().issuer_name(name)
           .last_update(now - timedelta(hours=1)).next_update(now + timedelta(days=7))
           .add_extension(x509.AuthorityKeyIdentifier.from_issuer_public_key(key.public_key()), critical=False)
           .add_extension(x509.CRLNumber(1), critical=False).sign(key, hashes.SHA256()))
    (directory / "ca.crl").write_bytes(crl.public_bytes(serialization.Encoding.DER))
    for server, wrong in (("server", False), ("wrong", True)):
        leaf_key = rsa.generate_private_key(public_exponent=65537, key_size=2048)
        subject = x509.Name([x509.NameAttribute(NameOID.COMMON_NAME, "wrong.example.invalid" if wrong else "127.0.0.1")])
        identity = x509.DNSName("wrong.example.invalid") if wrong else x509.IPAddress(ipaddress.ip_address("127.0.0.1"))
        leaf = (x509.CertificateBuilder().subject_name(subject).issuer_name(name)
                .public_key(leaf_key.public_key()).serial_number(x509.random_serial_number())
                .not_valid_before(now - timedelta(days=1)).not_valid_after(now + timedelta(days=7))
                .add_extension(x509.BasicConstraints(ca=False, path_length=None), critical=True)
                .add_extension(x509.SubjectAlternativeName([identity]), critical=False)
                .add_extension(x509.ExtendedKeyUsage([ExtendedKeyUsageOID.SERVER_AUTH]), critical=False)
                .add_extension(x509.AuthorityKeyIdentifier.from_issuer_public_key(key.public_key()), critical=False)
                .add_extension(point, critical=False).sign(key, hashes.SHA256()))
        (directory / f"{server}.pem").write_bytes(leaf.public_bytes(serialization.Encoding.PEM))
        (directory / f"{server}-key.pem").write_bytes(leaf_key.private_bytes(
            serialization.Encoding.PEM, serialization.PrivateFormat.PKCS8, serialization.NoEncryption()))
