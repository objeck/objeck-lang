import requests
import json

response = requests.get('http://worldtimeapi.org/api/ip')
if response.status_code == 200 :
	time_dist = json.loads(response.content)
	date_time_strs = time_dist['datetime'].split('T')

	date_str = date_time_strs[0]
	print(date_str)
	
	time_str = date_time_strs[1]
	index = time_str.find('.')
	if index > -1 :
		print(time_str[0 : index])