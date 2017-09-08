/*
 *  Phusion Passenger - https://www.phusionpassenger.com/
 *  Copyright (c) 2015-2017 Phusion Holding B.V.
 *
 *  "Passenger", "Phusion Passenger" and "Union Station" are registered
 *  trademarks of Phusion Holding B.V.
 *
 *  Permission is hereby granted, free of charge, to any person obtaining a copy
 *  of this software and associated documentation files (the "Software"), to deal
 *  in the Software without restriction, including without limitation the rights
 *  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 *  copies of the Software, and to permit persons to whom the Software is
 *  furnished to do so, subject to the following conditions:
 *
 *  The above copyright notice and this permission notice shall be included in
 *  all copies or substantial portions of the Software.
 *
 *  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 *  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 *  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 *  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 *  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 *  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 *  THE SOFTWARE.
 */
#ifndef _PASSENGER_API_ACCOUNT_UTILS_H_
#define _PASSENGER_API_ACCOUNT_UTILS_H_

#include <vector>
#include <string>
#include <cstddef>

#include <oxt/macros.hpp>
#include <boost/config.hpp>

#include <ConfigKit/ConfigKit.h>
#include <Exceptions.h>
#include <StaticString.h>
#include <Utils.h>
#include <Utils/StrIntUtils.h>

namespace Passenger {
namespace ApiAccountUtils { // Avoid conflict with classes of the same name in ApiServerUtils.h, until we've migrated everything

using namespace std;


/*
 * This file implements handling of API accounts.
 *
 * The various PassengerAgent ApiServers can be accessed through HTTP sockets.
 * Authenticating and authorizating clients is handled through API accounts.
 * Each ApiServer embeds an API account database. Each account contains a
 * username, password and a privilege level.
 *
 * The API account and the API account database are represented by the
 * `ApiAccount` and `ApiAccountDatabase` classes, respectively. Both of them
 * are supposed to be used in an immutable manner.
 *
 * Users can specify API accounts in two formats:
 *
 *  1. Through a JSON array:
 *
 *       [
 *         {
 *           "username": "foo",
 *
 *           // One of these must exist:
 *           "password": "bar",
 *           "password_file": "/filename"
 *
 *           "level": "readonly" | "full"   // Optional; "full" is defeault
 *         },
 *         ...
 *       ]
 *
 *  2. Through a list of description strings, each in the form of:
 *
 *       [LEVEL]:USERNAME:PASSWORDFILE
 *
 *     LEVEL is one of:
 *
 *       readonly    Read-only access
 *       full        Full access (default)
 *
 *
 * This file contains three functions for operating on input supplied in
 * one of the two formats (scroll down for descriptions):
 *
 *  - validateAuthorizationsField() -- checks whether a JSON array conforms to the above format.
 *  - normalizeApiAccountJson() -- normalizes an item in the JSON array into a consistent format.
 *  - parseApiAccountDescription() -- parses a description string into a JSON object.
 *
 * An authorizations JSON array is considered *valid* if it passes `validateAuthorizationsField()`.
 * An authorization JSON object is considered *normalized* if it conforms to the output format
 * generated by `normalizeApiAccountJson()`.
 *
 * Valid and normalized are orthogonal concepts. One does not imply the other.
 */


inline Json::Value parseApiAccountDescription(const StaticString &description);


/**
 * Checks whether an authorization JSON array conforms to the specified format.
 *
 * A JSON array that passes this function is *valid*, although not necessarily *normalized*.
 */
static void
validateAuthorizationsField(const string &key, const ConfigKit::Store &config,
	vector<ConfigKit::Error> &outputErrors)
{
	typedef ConfigKit::Error Error;

	if (config[key].isNull()) {
		return;
	}

	const Json::Value authorizations = config[key];
	Json::Value::const_iterator it, end = authorizations.end();
	vector<ConfigKit::Error> errors;

	for (it = authorizations.begin(); it != end; it++) {
		const Json::Value &auth = *it;

		if (auth.isString()) {
			try {
				parseApiAccountDescription(auth.asString());
			} catch (const ArgumentException &e) {
				errors.push_back(Error("'{{" + key + "}}' contains an invalid authorization description ("
					+ auth.asString() + "): " + e.what()));
			}
		} else if (auth.isObject()) {
			if (auth.isMember("username")) {
				if (!auth["username"].isString()) {
					errors.push_back(Error("All usernames in '{{" + key + "}}' must be strings"));
				} else if (auth["username"].asString() == "api") {
					errors.push_back(Error("'{{" + key + "}}' may not contain an 'api' username"));
				}
			} else {
				errors.push_back(Error("All objects in '{{" + key + "}}' must contain the 'username' key"));
			}

			if (auth.isMember("password")) {
				if (!auth["password"].isString()) {
					errors.push_back(Error("All passwords in '{{" + key + "}}' must be strings"));
				}
				if (auth.isMember("password_file")) {
					errors.push_back(Error("Entries in '{{" + key + "}}' must contain either the"
						" 'password' or the 'password_file' field, but not both"));
				}
			} else if (auth.isMember("password_file")) {
				if (!auth["password_file"].isString()) {
					errors.push_back(Error("All 'password_file' fields in '{{" + key + "}}' must be strings"));
				}
			} else {
				errors.push_back(Error("All objects in '{{" + key + "}}' must contain either the"
					" 'password' or 'password_file' key"));
			}

			if (auth.isMember("level")) {
				if (!auth["level"].isString() || (
					auth["level"].asString() != "readonly"
					&& auth["level"].asString() != "full"))
				{
					errors.push_back(Error("All 'level' fields in '{{" + key + "}}' must be either"
						" 'readonly' or 'full'"));
				}
			}
		} else {
			errors.push_back(Error("'{{" + key + "}}' may only contain strings or objects"));
		}
	}

	errors = ConfigKit::deduplicateErrors(errors);
	outputErrors.insert(outputErrors.end(), errors.begin(), errors.end());
}

/**
 * Given a *valid* authorizations JSON array, this function
 * turns that array into a consistent format.
 *
 * For example it ensures that, if the "level" field does
 * not exist, it is inserted with the default value.
 */
inline Json::Value
normalizeApiAccountJson(const Json::Value &json) {
	if (json.isString()) {
		return parseApiAccountDescription(json.asString());
	} else {
		Json::Value doc = json;
		if (json.isMember("password_file")) {
			doc["password_file"] = absolutizePath(json["password_file"].asString());
		}
		if (!json.isMember("level")) {
			doc["level"] = "full";
		}
		return doc;
	}
}

inline Json::Value
normalizeApiAccountsJson(const Json::Value &json) {
	Json::Value doc = json;
	Json::Value::iterator it, end = doc.end();
	for (it = doc.begin(); it != end; it++) {
		*it = normalizeApiAccountJson(*it);
	}
	return doc;
}

/**
 * Parses an API account description string into a *valid* (but not necessarily
 * *normalized*) JSON object.
 *
 * @throws ArgumentException One of the input arguments contains a disallowed value.
 */
inline Json::Value
parseApiAccountDescription(const StaticString &description) {
	Json::Value json;
	vector<StaticString> args;

	split(description, ':', args);

	if (args.size() == 2) {
		json["username"] = args[0].toString();
		json["password_file"] = args[1].toString();
		json["level"] = "full";
	} else if (args.size() == 3) {
		json["username"] = args[1].toString();
		json["password_file"] = args[2].toString();
		if (args[0] == "full" || args[0] == "readonly") {
			json["level"] = args[0].toString();
		} else {
			throw ArgumentException("'level' field must be either 'full' or 'readonly'");
		}
	} else {
		throw ArgumentException(string());
	}

	if (OXT_UNLIKELY(json["username"].asString() == "api")) {
		throw ArgumentException("the username 'api' is not allowed");
	}

	return json;
}

struct ApiAccount {
	string username;
	string password;
	bool readonly;

	/**
	 * Constructs an ApiAccount.
	 *
	 * @params doc An *normalized* authorization JSON object.
	 */
	ApiAccount(const Json::Value &doc) {
		username = doc["username"].asString();
		if (doc.isMember("password")) {
			password = doc["password"].asString();
		} else {
			password = strip(readAll(doc["password_file"].asString()));
		}
		readonly = doc["level"].asString() == "readonly";
	}
};

class ApiAccountDatabase {
private:
	vector<ApiAccount> database;

public:
	ApiAccountDatabase() { }

	/**
	 * Constructs an ApiAccountDatabase.
	 *
	 * @param authorizations A *normalized* JSON array of authorization objects.
	 */
	ApiAccountDatabase(const Json::Value &authorizations) {
		database.reserve(authorizations.size());

		Json::Value::const_iterator it, end = authorizations.end();
		for (it = authorizations.begin(); it != end; it++) {
			const Json::Value &auth = *it;
			database.push_back(ApiAccount(auth));
		}
	}

	bool empty() const {
		return database.empty();
	}

	const ApiAccount *lookup(const StaticString &username) const {
		vector<ApiAccount>::const_iterator it, end = database.end();

		for (it = database.begin(); it != end; it++) {
			if (it->username == username) {
				return &(*it);
			}
		}

		return NULL;
	}

	void swap(ApiAccountDatabase &other) BOOST_NOEXCEPT_OR_NOTHROW {
		database.swap(other.database);
	}
};


} // namespace ApiAccountUtils
} // namespace Passenger

#endif /* _PASSENGER_API_ACCOUNT_UTILS_H_*/
