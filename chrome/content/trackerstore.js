if (!org.bustany.TrackerBird.TrackerStore || !org.bustany.TrackerBird.TrackerStore.__initialized)
org.bustany.TrackerBird.TrackerStore = {
	// Init barrier
	__initialized: true,

	_graph: "urn:uuid:0aa84c52-396d-4c37-bc6c-8342699f810c",

	_addressParser: org.bustany.TrackerBird.EmailAddressParser,

	_tracker: org.bustany.TrackerBird.TrackerSparql,
	_connection: null,

	_knownEmailAddresses: {},

	init: function(connection) {
		this._connection = connection;
		return true;
	},

	storeMessage: function(folder, header, contents) {
		var uri = folder.getUriForMsg(header);
		var fromEmailAddress;
		var toEmailAddresses = [];

		var query = "";

		fromEmailAddress = this._addressParser.parse(header.mime2DecodedAuthor);
		if (fromEmailAddress) {
			query += this.insertEmailAddress(fromEmailAddress);
		}

		// FIXME: this is not bullet-proof, but will do for now
		var unparsedRecipients = header.mime2DecodedRecipients.split(">,");
		for (var i in unparsedRecipients) {
			var parsedAddress = this._addressParser.parse(unparsedRecipients[i] + ">");

			if (parsedAddress) {
				toEmailAddresses.push(parsedAddress);
				query += this.insertEmailAddress(parsedAddress);
			}
		}


		query += "INSERT { GRAPH <" + this._graph + "> {"
		       + this.baseQuery(uri, header)
		       + this.contactQuery("nmo:from", fromEmailAddress)
		       + this.contactsQuery("nmo:to", toEmailAddresses)
		       + this.contentsQuery(contents)
			   + "}}";

		if ((header.flags & Components.interfaces.nsMsgMessageFlags.Offline)
		 || (header.folder instanceof Components.interfaces.nsIMsgLocalMailFolder)) {
			 // We can access mail body
		}

		var error = new this._tracker.Error.ptr;
		this._tracker.connection_update(this._connection,
		                                query,
		                                100, /* batch */
		                                null,
		                                error.address());

		if (!error.isNull()) {
			dump("Cannot save message to Tracker: " + error.contents.message.readString() + "\n");
			dump("Query was\n" + query + "\n");
			this._tracker.error_free(error);
			return false;
		}

		dump("Inserted message " + uri + "\n");
		return true;
	},

	baseQuery: function(uri, hdr) {
		var query = "<" + uri + "> a nmo:Email, nmo:MailboxDataObject"
		          + "; nie:url \"" + uri + "\" ";

		if (hdr.mime2DecodedSubject != "") {
			query += "; nmo:messageSubject \""
			       + this.escapeString(hdr.mime2DecodedSubject)
				   + "\" ";
		}

		query += "; nmo:sentDate \""
		       + new Date(hdr.dateInSeconds).toISOString()
			   + "\" ";

		return query;
	},

	contactQuery: function(predicate, address) {
		if (address == null) {
			return "";
		}

		var nameSparql = "";

		if (address.name != "") {
			nameSparql = "; nco:fullname \""
			           + this.escapeString(address.name)
			           + "\" ";
		}

		var emailSparql = "; nco:hasEmailAddress <mailto:"
		                + address.address
		                + "> ";

		return "; " + predicate + " [ a nco:Contact " + nameSparql + emailSparql + "] ";
	},

	contactsQuery: function(predicate, addresses) {
		var query = "";

		for (var i in addresses) {
			query += this.contactQuery(predicate, addresses[i]);
		}

		return query;
	},

	contentsQuery: function(contents) {
		if (contents == null) {
			return "";
		}

		return "; nie:plainTextContent \""
		     + this.escapeString(contents)
		     + "\" ";
	},

	escapeString: function(str) {
		return this._tracker.escape_string(str).readString();
	},

	insertEmailAddress: function(address) {
		if (this._knownEmailAddresses[address.address]) {
			return "";
		}

		var query = "INSERT { GRAPH <" + this._graph + "> {"
		          + "<" + address.address + "> a nco:EmailAddress"
		          + "; nco:emailAddress \"" + address.address + "\"}}\n";

		this._knownEmailAddresses[address.address] = true;
		return query;
	}
}
