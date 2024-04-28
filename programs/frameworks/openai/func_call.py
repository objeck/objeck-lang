from flask import Flask
from flask import request
import json

app = Flask(__name__)

def post_actions(data):
    year = data["year"];
    if year == 1968:
        return '{"coach": "Tim Allen", "year": 1968}'
    else:
        return '{"coach": "Troy Bledsoe", "year": 1967}'

@app.route("/", methods=['GET', 'POST'])

def indx():
    if request.method == 'POST':
        if request.data:
            print(request.data)
            post_data = json.loads(request.data.decode(encoding='utf-8'))
            response = post_actions(post_data)
            print(response)            
            if response:
                return response
            else:
                return '200'
        else:
            return '404'

    elif request.method == 'GET':
        return 'hello'
        
    else:
        return '404'

if __name__ == "__main__":
    app.run(host='localhost', port='5000')