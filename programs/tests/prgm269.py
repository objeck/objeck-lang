from datetime import datetime
import requests
import json

def parse_time(time_dist):
	date_time_parts = time_dist['datetime'].split('T')
	if len(date_time_parts) == 2 :
		date_parts = date_time_parts[0].split('-')
		year = date_parts[0]
		mon = date_parts[1]
		day = date_parts[2]

	time_str = date_time_parts[1]
	index = time_str.find('.')
	if index > -1 :
		time_parts = time_str[0 : index].split(':')
		if len(time_parts) == 3 :
			hrs = time_parts[0]
			mins = time_parts[1]
			secs = time_parts[2]

			date_time_str = "{}/{}/{} {}:{}:{}".format(mon, day, year, hrs, mins, secs)
			return datetime.strptime(date_time_str, '%m/%d/%Y %H:%M:%S')

response = requests.get('http://worldtimeapi.org/api/ip')
if response.status_code == 200 :
	print(parse_time(json.loads(response.content)))