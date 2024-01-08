import requests
import json

response = requests.get('http://worldtimeapi.org/api/ip')
if response.status_code == 200 :
	time_dist = json.loads(response.content)
	date_time_parts = time_dist['datetime'].split('T')

	date_parts = date_time_parts[0].split(':')
	if len(date_parts) == 3 :
		year = time_parts[0]
		mon = time_parts[1]
		day = time_parts[2]

	time_str = date_time_parts[1]
	index = time_str.find('.')
	if index > -1 :
		time_parts = time_str[0 : index].split(':')
		if len(time_parts) == 3 :
			hrs = time_parts[0]
			mins = time_parts[1]
			secs = time_parts[2]

# TODO: convert to a string to a date
# datetime.strptime(datetime_str, '%m/%d/%y %H:%M:%S')