import json
 
# Opening JSON file
f = open('C:\\Users\\objec\\Documents\\Temp\\Web.MsHtml.json')
 
# returns JSON object as 
# a dictionary
data = json.load(f)
 
const = data['Constants']
first = const[0];
name = first['Name']
print(name)
 
# Closing file
f.close()