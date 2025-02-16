namespace client_win
{
	class UserData
	{
	}

	public class LocationService
	{
		public async Task<Geoposition> GetCurrentLocationAsync()
		{
			// Create a Geolocator instance with high accuracy.
			Geolocator geolocator = new Geolocator { DesiredAccuracy = PositionAccuracy.High };

			try
			{
				// Request the current position.
				Geoposition position = await geolocator.GetGeopositionAsync();
				return position;
			}
			catch (UnauthorizedAccessException)
			{
				// The app doesn't have permission to access location.
				throw new Exception("Location access is denied. Please enable location permissions.");
			}
			catch (Exception ex)
			{
				// Handle other exceptions.
				throw new Exception("An error occurred while retrieving location: " + ex.Message);
			}
		}
	}
}
