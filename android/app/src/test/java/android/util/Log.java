package android.util;

/**
 * JVM unit tests run against android.jar's stubs, whose Log methods throw
 * "not mocked".  This test-only stand-in prints instead, so engine paths
 * that log an error (an unreadable save, a failed server call) can be
 * tested.
 */
public final class Log {
    private Log() {}

    private static int print(String level, String tag, String msg, Throwable tr) {
        System.out.println(level + "/" + tag + ": " + msg + (tr != null ? " (" + tr + ")" : ""));
        return 0;
    }

    public static int v(String tag, String msg) { return print("V", tag, msg, null); }
    public static int v(String tag, String msg, Throwable tr) { return print("V", tag, msg, tr); }
    public static int d(String tag, String msg) { return print("D", tag, msg, null); }
    public static int d(String tag, String msg, Throwable tr) { return print("D", tag, msg, tr); }
    public static int i(String tag, String msg) { return print("I", tag, msg, null); }
    public static int i(String tag, String msg, Throwable tr) { return print("I", tag, msg, tr); }
    public static int w(String tag, String msg) { return print("W", tag, msg, null); }
    public static int w(String tag, String msg, Throwable tr) { return print("W", tag, msg, tr); }
    public static int w(String tag, Throwable tr) { return print("W", tag, "", tr); }
    public static int e(String tag, String msg) { return print("E", tag, msg, null); }
    public static int e(String tag, String msg, Throwable tr) { return print("E", tag, msg, tr); }
    public static boolean isLoggable(String tag, int level) { return false; }
    public static String getStackTraceString(Throwable tr) { return tr == null ? "" : tr.toString(); }
}
