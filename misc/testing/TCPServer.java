import java.io.*;
import java.net.*;

public class TCPServer {
    public static void main(String args[]) throws Exception {
        ServerSocket listener = new ServerSocket(9090);
        Socket socket = null;
        try {
            while(true) {
                socket = listener.accept();
                BufferedOutputStream out = new BufferedOutputStream(socket.getOutputStream());
                out.write("안, 蠀, ☃\n".getBytes("UTF-8"));
								// new java.util.Date().toString());
								out.flush();
            }
        }
        finally {
            listener.close();
            if(socket != null) {
                socket.close();
            }
        }
    }
}
