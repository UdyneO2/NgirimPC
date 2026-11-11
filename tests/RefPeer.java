// Rekan referensi Ngirim v2 memakai java.io.Data*Stream asli (sama dengan aplikasi Android).
// Dipakai hanya untuk uji interop; logika dipindahkan dari FileService.kt.
import java.io.*;
import java.net.*;
import java.nio.file.*;
import java.util.*;
import javax.crypto.Mac;
import javax.crypto.spec.SecretKeySpec;

public class RefPeer {
    static final int MAGIC = 0x4E475231;
    static final String NAME = "Java-\u00e9\u4e2d\uD83D\uDE00";

    static byte[] hmac(String pin, byte[] nonce, int mode, String peer) throws Exception {
        Mac m = Mac.getInstance("HmacSHA256");
        m.init(new SecretKeySpec(pin.getBytes(), "HmacSHA256"));
        m.update(nonce); m.update((byte) mode); m.update(peer.getBytes());
        return m.doFinal();
    }

    record Ent(String rel, File f, boolean dir, long size) {}

    static void walk(String rel, File f, List<Ent> out) {
        if (f.isDirectory()) {
            out.add(new Ent(rel, f, true, 0));
            File[] k = f.listFiles();
            Arrays.sort(k, Comparator.comparing(File::getName));
            for (File c : k) walk(rel + "/" + c.getName(), c, out);
        } else out.add(new Ent(rel, f, false, f.length()));
    }

    static void sendSession(DataInputStream in, DataOutputStream out, List<Ent> list) throws Exception {
        long total = 0; for (Ent e : list) total += e.size;
        out.writeInt(list.size()); out.writeLong(total);
        for (Ent e : list) { out.writeUTF(e.rel); out.writeByte(e.dir ? 1 : 0); out.writeLong(e.size); }
        out.flush();
        if (in.readByte() != 1) throw new IOException("Ditolak: " + in.readUTF());
        in.readUTF();
        byte[] buf = new byte[1 << 16];
        for (Ent e : list) {
            if (e.dir) continue;
            try (FileInputStream fi = new FileInputStream(e.f)) {
                long left = e.size;
                while (left > 0) {
                    int n = fi.read(buf, 0, (int) Math.min(left, buf.length));
                    out.write(buf, 0, n); left -= n;
                }
            }
        }
        out.flush();
        if (in.readByte() != 1) throw new IOException("Penerima gagal");
    }

    static void recvSession(DataInputStream in, DataOutputStream out, File root) throws Exception {
        int count = in.readInt(); long total = in.readLong();
        List<Ent> list = new ArrayList<>();
        for (int i = 0; i < count; i++) list.add(new Ent(in.readUTF(), null, in.readByte() == 1, in.readLong()));
        out.writeByte(1); out.writeUTF("SIAP"); out.flush();
        byte[] buf = new byte[1 << 16];
        for (Ent e : list) {
            File t = new File(root, e.rel);
            if (e.dir) { t.mkdirs(); continue; }
            t.getParentFile().mkdirs();
            try (FileOutputStream fo = new FileOutputStream(t)) {
                long left = e.size;
                while (left > 0) {
                    int n = in.read(buf, 0, (int) Math.min(left, buf.length));
                    if (n < 0) throw new EOFException();
                    fo.write(buf, 0, n); left -= n;
                }
            }
            System.out.println("RECV " + e.rel + " " + e.size);
        }
        out.writeByte(1); out.flush();
    }

    static Object[] connect(String host, int port, String pin, int mode) throws Exception {
        Socket s = new Socket(host, port);
        s.setSoTimeout(20000);
        DataInputStream in = new DataInputStream(new BufferedInputStream(s.getInputStream()));
        DataOutputStream out = new DataOutputStream(new BufferedOutputStream(s.getOutputStream()));
        if (in.readInt() != MAGIC) throw new IOException("magic");
        if (in.readByte() != 2) throw new IOException("versi");
        byte[] nonce = new byte[16]; in.readFully(nonce);
        String server = in.readUTF();
        out.writeByte(mode); out.writeUTF(NAME); out.write(hmac(pin, nonce, mode, NAME)); out.flush();
        if (in.readByte() != 1) { System.out.println("AUTHFAIL " + in.readUTF()); s.close(); return null; }
        in.readUTF();
        System.out.println("SERVER " + server);
        return new Object[]{s, in, out};
    }

    public static void main(String[] a) throws Exception {
        String cmd = a[0];
        if (cmd.equals("serve")) {           // serve <pin> <destDir> <connections>
            String pin = a[1]; File dest = new File(a[2]); int n = Integer.parseInt(a[3]);
            ServerSocket ss = new ServerSocket(0);
            System.out.println("PORT " + ss.getLocalPort()); System.out.flush();
            for (int i = 0; i < n; i++) {
                Socket s = ss.accept(); s.setSoTimeout(20000);
                DataInputStream in = new DataInputStream(new BufferedInputStream(s.getInputStream()));
                DataOutputStream out = new DataOutputStream(new BufferedOutputStream(s.getOutputStream()));
                byte[] nonce = new byte[16]; new Random().nextBytes(nonce);
                out.writeInt(MAGIC); out.writeByte(2); out.write(nonce); out.writeUTF(NAME); out.flush();
                int mode = in.readByte(); String peer = in.readUTF(); byte[] mac = new byte[32]; in.readFully(mac);
                System.out.println("PEER " + peer + " mode=" + mode);
                if (!MessageDigest.isEqual(mac, hmac(pin, nonce, mode, peer))) {
                    out.writeByte(0); out.writeUTF("PIN salah"); out.flush(); s.close(); continue;
                }
                out.writeByte(1); out.writeUTF("OK"); out.flush();
                if (mode == 2) recvSession(in, out, dest);
                s.close();
            }
            System.out.println("DONE");
            return;
        }
        String host = a[1]; int port = Integer.parseInt(a[2]); String pin = a[3];
        switch (cmd) {
            case "ping": {
                Object[] c = connect(host, port, pin, 1);
                if (c != null) System.out.println("PAIROK");
                break;
            }
            case "send": {                    // send ... <paths...>
                Object[] c = connect(host, port, pin, 2); if (c == null) break;
                List<Ent> l = new ArrayList<>();
                for (int i = 4; i < a.length; i++) { File f = new File(a[i]); walk(f.getName(), f, l); }
                sendSession((DataInputStream) c[1], (DataOutputStream) c[2], l);
                System.out.println("SENDOK");
                break;
            }
            case "evil": {                    // path traversal harus ditolak
                Object[] c = connect(host, port, pin, 2); if (c == null) break;
                DataInputStream in = (DataInputStream) c[1]; DataOutputStream out = (DataOutputStream) c[2];
                out.writeInt(1); out.writeLong(4); out.writeUTF("../evil.txt"); out.writeByte(0); out.writeLong(4); out.flush();
                int ok = in.readByte(); String m = in.readUTF();
                System.out.println("EVIL ok=" + ok + " msg=" + m);
                break;
            }
            case "list": {                    // list <path>
                Object[] c = connect(host, port, pin, 3); if (c == null) break;
                DataInputStream in = (DataInputStream) c[1]; DataOutputStream out = (DataOutputStream) c[2];
                out.writeUTF(a[4]); out.flush();
                if (in.readByte() != 1) { System.out.println("LISTFAIL " + in.readUTF()); break; }
                in.readUTF();
                int n = in.readInt();
                for (int i = 0; i < n; i++) System.out.println("ENT " + in.readUTF() + " " + in.readLong() + " " + in.readByte());
                break;
            }
            case "pull": {                    // pull <destDir> <remotePaths...>
                Object[] c = connect(host, port, pin, 4); if (c == null) break;
                DataInputStream in = (DataInputStream) c[1]; DataOutputStream out = (DataOutputStream) c[2];
                out.writeInt(a.length - 5);
                for (int i = 5; i < a.length; i++) out.writeUTF(a[i]);
                out.flush();
                if (in.readByte() != 1) { System.out.println("PULLFAIL " + in.readUTF()); break; }
                in.readUTF();
                recvSession(in, out, new File(a[4]));
                System.out.println("PULLOK");
                break;
            }
            case "push": {                    // push <remoteDir> <paths...>
                Object[] c = connect(host, port, pin, 5); if (c == null) break;
                DataInputStream in = (DataInputStream) c[1]; DataOutputStream out = (DataOutputStream) c[2];
                out.writeUTF(a[4]); out.flush();
                if (in.readByte() != 1) { System.out.println("PUSHFAIL " + in.readUTF()); break; }
                in.readUTF();
                List<Ent> l = new ArrayList<>();
                for (int i = 5; i < a.length; i++) { File f = new File(a[i]); walk(f.getName(), f, l); }
                sendSession(in, out, l);
                System.out.println("PUSHOK");
                break;
            }
        }
    }
    static class MessageDigest { static boolean isEqual(byte[] x, byte[] y) { return java.security.MessageDigest.isEqual(x, y); } }
}
