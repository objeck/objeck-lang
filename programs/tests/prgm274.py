import json
 
file = open('C:\\Users\\objec\\Documents\\Temp\\Web.MsHtml.json', 'r')
data = json.load(file)
 
const = data['Constants']
fileirst = const[0];
name = fileirst['Name']
print(name)

file.close()