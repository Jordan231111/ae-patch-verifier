import com.android.apksig.ApkVerifier;
import com.google.gson.JsonObject;
import com.google.gson.JsonParser;
import pxb.android.axml.Axml;
import pxb.android.axml.AxmlReader;
import pxb.android.axml.AxmlWriter;
import pxb.android.axml.NodeVisitor;

import java.io.BufferedOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.InputStreamReader;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.security.MessageDigest;
import java.util.Arrays;
import java.util.HexFormat;
import java.util.Objects;
import java.util.zip.CRC32;
import java.util.zip.ZipEntry;
import java.util.zip.ZipFile;
import java.util.zip.ZipOutputStream;

/**
 * Adds host metadata only, using libraries already present in LSPatch v1.2's jar.
 * java -cp lspatch.jar PrepareMicrog.java ORIGINAL.apk LSPATCHED.apk UNSIGNED_OUT.apk TRUSTED_COMPANION.apk
 * Run after LSPatch, before zipalign and signing. No DEX/resource-table edits.
 */
public class PrepareMicrog {
    private static final String ANDROID = "http://schemas.android.com/apk/res/android";
    private static final String MICROG = "app.revanced.android.gms";
    private static final String OPT_IN = "com.jordan.aepcdtrace.MICROG_PLAY_GAMES";
    private static final String IDENTITY_OPT_IN = "com.jordan.aepcdtrace.MICROG_IDENTITY_SIGN_IN";
    private static final String COMPANION_CERT = "com.jordan.aepcdtrace.MICROG_COMPANION_CERTIFICATE";
    private static final String MANIFEST = "AndroidManifest.xml";
    private static final String ORIGIN = "assets/lspatch/origin.apk";
    private static final String CONFIG = "assets/lspatch/config.json";
    private static final String MODULE = "assets/lspatch/modules/com.jordan.aepcdtrace.apk";
    // Stable public android.R.attr IDs. This host-side tool does not load android.jar.
    private static final int ANDROID_ATTR_NAME = 0x01010003;
    private static final int ANDROID_ATTR_VALUE = 0x01010024;

    public static void main(String[] args) throws Exception {
        if (args.length != 4) throw new IllegalArgumentException(
                "Usage: java -cp lspatch.jar PrepareMicrog.java ORIGINAL.apk LSPATCHED.apk UNSIGNED_OUT.apk TRUSTED_COMPANION.apk");
        Path source = Path.of(args[0]), patched = Path.of(args[1]), output = Path.of(args[2]).toAbsolutePath();
        if (Files.exists(output)) throw new IllegalArgumentException("Output already exists: " + output);
        String sha1 = signerDigest(source, "SHA-1");
        String companionCertificate;
        {
            Path companion = Path.of(args[3]);
            companionCertificate = signerDigest(companion, "SHA-256");
            try (ZipFile companionZip = new ZipFile(companion.toFile())) {
                if (!MICROG.equals(attribute(root(read(companionZip)), null, "package"))) {
                    throw new IllegalArgumentException("Companion package must be " + MICROG);
                }
            }
        }
        try (ZipFile original = new ZipFile(source.toFile()); ZipFile host = new ZipFile(patched.toFile())) {
            if (original.getEntry(ORIGIN) != null) throw new IllegalArgumentException("Source is already LSPatched");
            validateHost(host);
            try (InputStream input = Files.newInputStream(source); InputStream embedded = host.getInputStream(required(host, ORIGIN))) {
                if (!sameContents(input, embedded)) {
                    throw new IllegalArgumentException("LSPatch origin does not match the original source APK");
                }
            }
            Axml sourceXml = read(original), hostXml = read(host);
            Axml.Node sourceRoot = root(sourceXml), hostRoot = root(hostXml);
            Object pkg = attribute(sourceRoot, null, "package");
            if (!(pkg instanceof String) || !pkg.equals(attribute(hostRoot, null, "package"))) {
                throw new IllegalArgumentException("Source and host package names must match");
            }
            Axml.Node app = child(hostRoot, "application", false);
            if (app == null) throw new IllegalArgumentException("No host application element");
            // Idempotent on an already prepared outer APK; the original identity always comes
            // from the verified source, never the new signing key or a hardcoded game hash.
            metadata(app, MICROG + ".SPOOFED_PACKAGE_NAME", NodeVisitor.TYPE_STRING, pkg);
            metadata(app, MICROG + ".SPOOFED_PACKAGE_SIGNATURE", NodeVisitor.TYPE_STRING, sha1);
            metadata(app, OPT_IN, NodeVisitor.TYPE_INT_BOOLEAN, -1);
            metadata(app, IDENTITY_OPT_IN, NodeVisitor.TYPE_INT_BOOLEAN, -1);
            metadata(app, COMPANION_CERT, NodeVisitor.TYPE_STRING, companionCertificate);
            Axml.Node queries = child(hostRoot, "queries", true);
            boolean visible = queries.children.stream().anyMatch(n -> "package".equals(n.name)
                    && MICROG.equals(attribute(n, ANDROID, "name")));
            if (!visible) {
                Axml.Node query = (Axml.Node) queries.child(null, "package");
                query.attr(ANDROID, "name", ANDROID_ATTR_NAME, NodeVisitor.TYPE_STRING, MICROG);
            }
            String providerPermission = "android.permission.CREDENTIAL_MANAGER_SET_ALLOWED_PROVIDERS";
            if (hostRoot.children.stream().noneMatch(n -> "uses-permission".equals(n.name)
                    && providerPermission.equals(attribute(n, ANDROID, "name")))) {
                Axml.Node permission = (Axml.Node) hostRoot.child(null, "uses-permission");
                permission.attr(ANDROID, "name", ANDROID_ATTR_NAME, NodeVisitor.TYPE_STRING, providerPermission);
            }
            AxmlWriter writer = new AxmlWriter();
            hostXml.accept(writer);
            byte[] manifest = writer.toByteArray();
            // Reparse before emitting an artifact; fail on unsupported AXML rather than
            // silently falling back to a resource/DEX rebuild.
            Axml roundTrip = new Axml();
            new AxmlReader(manifest).accept(roundTrip);
            if (!pkg.equals(attribute(root(roundTrip), null, "package"))) {
                throw new IllegalStateException("Manifest round-trip failed");
            }
            writeArchive(host, output, manifest);
            System.out.println("Prepared shared MicroG Games + Identity login: " + pkg + " original SHA-1=" + sha1);
            System.out.println("Only outer AndroidManifest.xml changed; zipalign and sign the output before installation.");
        }
    }

    private static String signerDigest(Path apk, String algorithm) throws Exception {
        ApkVerifier.Result verification = new ApkVerifier.Builder(apk.toFile()).build().verify();
        if (!verification.isVerified() || verification.getSignerCertificates().size() != 1) {
            throw new IllegalArgumentException("APK must verify with exactly one signer: " + apk);
        }
        return HexFormat.of().withUpperCase().formatHex(MessageDigest.getInstance(algorithm)
                .digest(verification.getSignerCertificates().get(0).getEncoded()));
    }

    private static void validateHost(ZipFile host) throws IOException {
        if (host.getEntry(MODULE) == null) {
            throw new IllegalArgumentException("Embed the AE module with -m before preparing MicroG login");
        }
        JsonObject config;
        try (InputStream in = host.getInputStream(required(host, CONFIG))) {
            config = JsonParser.parseReader(new InputStreamReader(in, StandardCharsets.UTF_8)).getAsJsonObject();
        }
        if (config.get("useManager").getAsBoolean() || config.get("sigBypassLevel").getAsInt() != 2) {
            throw new IllegalArgumentException("MicroG module routing requires integrated mode and signature-bypass level 2");
        }
        var replacement = config.get("gmsReplacementPackage");
        if (replacement != null && !replacement.isJsonNull() && !replacement.getAsString().isEmpty()) {
            throw new IllegalArgumentException("Omit --gms-replacement: the embedded module owns the selected login routes");
        }
    }

    private static void writeArchive(ZipFile host, Path output, byte[] manifest) throws IOException {
        Path temporary = Files.createTempFile(output.getParent(), ".prepare-microg-", ".apk");
        try {
            try (ZipOutputStream out = new ZipOutputStream(new BufferedOutputStream(Files.newOutputStream(temporary), 65536))) {
                var entries = host.entries();
                while (entries.hasMoreElements()) {
                    ZipEntry entry = entries.nextElement();
                    ZipEntry copy = new ZipEntry(entry.getName());
                    copy.setTime(entry.getTime());
                    copy.setMethod(entry.getMethod());
                    boolean changed = MANIFEST.equals(entry.getName());
                    if (copy.getMethod() == ZipEntry.STORED) {
                        CRC32 crc = new CRC32();
                        if (changed) crc.update(manifest);
                        copy.setSize(changed ? manifest.length : entry.getSize());
                        copy.setCompressedSize(copy.getSize());
                        copy.setCrc(changed ? crc.getValue() : entry.getCrc());
                    }
                    out.putNextEntry(copy);
                    if (changed) out.write(manifest);
                    else try (InputStream in = host.getInputStream(entry)) { in.transferTo(out); }
                    out.closeEntry();
                }
            }
            Files.move(temporary, output);
        } finally {
            Files.deleteIfExists(temporary);
        }
    }

    private static boolean sameContents(InputStream source, InputStream embedded) throws IOException {
        byte[] left = new byte[65536];
        byte[] right = new byte[left.length];
        for (;;) {
            int count = source.readNBytes(left, 0, left.length);
            int embeddedCount = embedded.readNBytes(right, 0, right.length);
            if (count != embeddedCount || !Arrays.equals(left, 0, count, right, 0, embeddedCount)) return false;
            if (count < left.length) return true;
        }
    }

    private static ZipEntry required(ZipFile archive, String name) {
        ZipEntry entry = archive.getEntry(name);
        if (entry == null) throw new IllegalArgumentException("Missing " + name);
        return entry;
    }

    private static Axml read(ZipFile archive) throws Exception {
        Axml xml = new Axml();
        try (InputStream in = archive.getInputStream(required(archive, MANIFEST))) {
            new AxmlReader(in.readAllBytes()).accept(xml);
        }
        return xml;
    }

    private static Axml.Node root(Axml xml) {
        if (xml.firsts.size() != 1 || !"manifest".equals(xml.firsts.get(0).name)) {
            throw new IllegalArgumentException("Expected one manifest root");
        }
        return xml.firsts.get(0);
    }

    private static Object attribute(Axml.Node node, String namespace, String name) {
        return node.attrs.stream().filter(a -> Objects.equals(namespace, a.ns) && name.equals(a.name))
                .map(a -> a.value).findFirst().orElse(null);
    }

    private static Axml.Node child(Axml.Node parent, String name, boolean create) {
        for (Axml.Node node : parent.children) if (name.equals(node.name)) return node;
        return create ? (Axml.Node) parent.child(null, name) : null;
    }

    private static void metadata(Axml.Node app, String name, int type, Object value) {
        app.children.removeIf(n -> "meta-data".equals(n.name) && name.equals(attribute(n, ANDROID, "name")));
        Axml.Node meta = (Axml.Node) app.child(null, "meta-data");
        meta.attr(ANDROID, "name", ANDROID_ATTR_NAME, NodeVisitor.TYPE_STRING, name);
        meta.attr(ANDROID, "value", ANDROID_ATTR_VALUE, type, value);
    }
}
