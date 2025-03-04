package com.example.androidclient

import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.enableEdgeToEdge
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.tooling.preview.Preview
import androidx.lifecycle.lifecycleScope
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch
import kotlinx.coroutines.Dispatchers
import java.net.DatagramPacket
import java.net.DatagramSocket
import java.net.InetAddress
import com.example.androidclient.ui.theme.AndroidClientTheme
import android.util.Log
import java.nio.ByteBuffer
import java.time.Instant

private var seq: UInt = 0u

data class Packet(
    var type             : UInt,
    var	seq_num	         : UInt,
    var	time_client_send : ULong,
    var	time_server_recv : ULong,
    var	time_server_send : ULong,
    var	time_client_recv : ULong)

fun Packet.toByteArray(): ByteArray {
    val buffer = ByteBuffer.allocate(4 + 4 + 8 + 8 + 8 + 8)
    buffer.putInt(type.toInt())
    buffer.putInt(seq_num.toInt())
    buffer.putLong(time_client_send.toLong())
    buffer.putLong(time_server_recv.toLong())
    buffer.putLong(time_server_send.toLong())
    buffer.putLong(time_client_recv.toLong())
    return buffer.array()
}

class MainActivity : ComponentActivity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        enableEdgeToEdge()
        setContent {
            AndroidClientTheme {
                Scaffold(modifier = Modifier.fillMaxSize()) { innerPadding ->
                    Greeting(
                        name = "Android",
                        modifier = Modifier.padding(innerPadding)
                    )
                }
            }
        }

        lifecycleScope.launch(Dispatchers.IO) {
            sendUdpPackets()
        }
    }
}

private suspend fun sendUdpPackets() {
    try {
        DatagramSocket().use { socket ->
            while (true) {
                val targetAddress: InetAddress = InetAddress.getByName("2001:2d8:f219:c1d2:917e:6c27:8798:dcdb")
                val targetPort: Int = 5050
                // Define the message to send.
                val now = Instant.now()
                val epochNanos = now.epochSecond * 1_000_000_000 + now.nano

                var p = Packet(0u,seq,0u,0u,0u,0u)

                p.time_client_send = epochNanos.toULong();
                val buffer = p.toByteArray()

                // Create the DatagramPacket with the target IPv6 address and port.
                val packet = DatagramPacket(buffer, buffer.size, targetAddress, targetPort)

                // Send the UDP packet.
                socket.send(packet)
                // For debugging: Log or print that a packet was sent.
                Log.d("UDPClient", "Packet sent to $targetAddress:$targetPort, seq:$seq")
                seq = seq + 1u
                // Wait 1 second before sending the next packet.
                delay(1000L)
            }
        }
    }
    catch (e : Exception){
        println(e);
        var a = 0;
    }
    finally{

    }
    // Create a DatagramSocket (automatically selects an available port).

}

@Composable
fun Greeting(name: String, modifier: Modifier = Modifier) {
    Text(
        text = "Hello $name!\nseq : $seq",
        modifier = modifier
    )
}

@Preview(showBackground = true)
@Composable
fun GreetingPreview() {
    AndroidClientTheme {
        Greeting("Android")
    }
}