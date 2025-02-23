using System;
using System.Collections.Generic;
using System.Device.Location;
using System.Diagnostics;
using System.Net.NetworkInformation;
using System.Threading;

namespace win_client
{
	internal class Program
	{
		static void Main(string[] args)
		{
			PerformanceCounterCategory category = new PerformanceCounterCategory("Network Interface");
			string[] instanceNames = category.GetInstanceNames();

			GeoCoordinateWatcher watcher = new GeoCoordinateWatcher();

			// Attach event handlers to track status and position changes.
			watcher.StatusChanged += (sender, e) =>
			{
				Console.WriteLine("Status: " + e.Status);
			};

			watcher.PositionChanged += (sender, e) =>
			{
				GeoCoordinate coord = e.Position.Location;
				if (!coord.IsUnknown)
				{
					Console.WriteLine("Latitude: " + coord.Latitude);
					Console.WriteLine("Longitude: " + coord.Longitude);
				}
				else
				{
					Console.WriteLine("Location data is unknown.");
				}
			};

			// Start the watcher. Optionally, you can use TryStart() with a timeout.
			watcher.Start();


			var previousStats = new Dictionary<string, (long bytesSent, long bytesReceived)>();

			foreach (NetworkInterface ni in NetworkInterface.GetAllNetworkInterfaces())
			{
				if (ni.OperationalStatus == OperationalStatus.Up &&
					ni.NetworkInterfaceType != NetworkInterfaceType.Loopback)
				{
					var stats = ni.GetIPv4Statistics();
					previousStats[ni.Id] = (stats.BytesSent, stats.BytesReceived);
				}
			}

			PerformanceCounter cpuCounter = new PerformanceCounter("Processor", "% Processor Time", "_Total");
			cpuCounter.NextValue();
			for (var i = 0; i < 10; ++i)
			{
				foreach (NetworkInterface ni in NetworkInterface.GetAllNetworkInterfaces())
				{
					if (ni.OperationalStatus == OperationalStatus.Up && ni.NetworkInterfaceType != NetworkInterfaceType.Loopback && previousStats.ContainsKey(ni.Id))
					{
						var stats = ni.GetIPStatistics();
						long previousSent = previousStats[ni.Id].bytesSent;
						long previousReceived = previousStats[ni.Id].bytesReceived;
						long currentSent = stats.BytesSent;
						long currentReceived = stats.BytesReceived;

						long deltaSent = currentSent - previousSent;
						long deltaReceived = currentReceived - previousReceived;

						Console.WriteLine($"Interface: {ni.Name}");
						Console.WriteLine($"Bytes Sent/sec: {deltaSent}");
						Console.WriteLine($"Bytes Received/sec: {deltaReceived}");
						Console.WriteLine($"Total Bytes Sent: {currentSent}");
						Console.WriteLine($"Total Bytes Received: {currentReceived}");
						Console.WriteLine("-------------------------------------");
					}
				}

				float cpuUsage = cpuCounter.NextValue();
				Console.WriteLine("CPU Usage: " + cpuUsage + " %");
				Thread.Sleep(1000);
			}



			Console.WriteLine("Waiting for location update. Press Enter to exit...");
			Console.ReadLine();
		}
	}
}
