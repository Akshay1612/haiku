/*
 * Copyright 2010, Christophe Huriaux
 * Copyright 2014-2020, Haiku, inc.
 * Distributed under the terms of the MIT licence
 */


#include "HttpTest.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <posix/libgen.h>
#include <string>

#include <AutoDeleter.h>
#include <HttpRequest.h>
#include <NetworkKit.h>
#include <UrlProtocolListener.h>

#include <tools/cppunit/ThreadedTestCaller.h>

#include "TestServer.h"


namespace {

typedef std::map<std::string, std::string> HttpHeaderMap;


class TestListener : public BUrlProtocolListener {
public:
	TestListener(const std::string& expectedResponseBody,
				 const HttpHeaderMap& expectedResponseHeaders)
		:
		fExpectedResponseBody(expectedResponseBody),
		fExpectedResponseHeaders(expectedResponseHeaders)
	{
	}

	virtual void DataReceived(
		BUrlRequest *caller,
		const char *data,
		off_t position,
		ssize_t size)
	{
		std::copy_n(
			data + position,
			size,
			std::back_inserter(fActualResponseBody));
	}

	virtual void HeadersReceived(
		BUrlRequest* caller,
		const BUrlResult& result)
	{
		const BHttpResult& http_result
			= dynamic_cast<const BHttpResult&>(result);
		const BHttpHeaders& headers = http_result.Headers();

		for (int32 i = 0; i < headers.CountHeaders(); ++i) {
			const BHttpHeader& header = headers.HeaderAt(i);
			fActualResponseHeaders[std::string(header.Name())]
				= std::string(header.Value());
		}
	}


	virtual bool CertificateVerificationFailed(
		BUrlRequest* caller,
		BCertificate& certificate,
		const char* message)
	{
		// TODO: Add tests that exercize this behavior.
		//
		// At the moment there doesn't seem to be any public API for providing
		// an alternate certificate authority, or for constructing a
		// BCertificate to be sent to BUrlContext::AddCertificateException().
		// Once we have such a public API then it will be useful to create
		// test scenarios that exercize the validation performed by the
		// undrelying TLS implementaiton to verify that it is configured
		// to do so.
		//
		// For now we just disable TLS certificate validation entirely because
		// we are generating a self-signed TLS certificate for these tests.
		return true;
	}


	void Verify()
	{
		CPPUNIT_ASSERT_EQUAL(fExpectedResponseBody, fActualResponseBody);

		for (HttpHeaderMap::iterator iter = fActualResponseHeaders.begin();
			 iter != fActualResponseHeaders.end();
			 ++iter)
		{
			CPPUNIT_ASSERT_EQUAL_MESSAGE(
				"(header " + iter->first + ")",
				fExpectedResponseHeaders[iter->first],
				iter->second);
		}
		CPPUNIT_ASSERT_EQUAL(
			fExpectedResponseHeaders.size(),
			fActualResponseHeaders.size());
	}

private:
	std::string fExpectedResponseBody;
	std::string fActualResponseBody;

	HttpHeaderMap fExpectedResponseHeaders;
	HttpHeaderMap fActualResponseHeaders;
};


void SendAuthenticatedRequest(
	BUrlContext &context,
	BUrl &testUrl,
	const std::string& expectedResponseBody,
	const HttpHeaderMap &expectedResponseHeaders)
{
	TestListener listener(expectedResponseBody, expectedResponseHeaders);

	BHttpRequest request(testUrl, testUrl.Protocol() == "https");
	request.SetContext(&context);
	request.SetListener(&listener);

	request.SetUserName("walter");
	request.SetPassword("secret");

	CPPUNIT_ASSERT(request.Run());

	while (request.IsRunning())
		snooze(1000);

	CPPUNIT_ASSERT_EQUAL(B_OK, request.Status());

	const BHttpResult &result =
		dynamic_cast<const BHttpResult &>(request.Result());
	CPPUNIT_ASSERT_EQUAL(200, result.StatusCode());
	CPPUNIT_ASSERT_EQUAL(BString("OK"), result.StatusText());

	listener.Verify();
}


// Return the path of a file path relative to this source file.
std::string TestFilePath(const std::string& relativePath)
{
	char *testFileSource = strdup(__FILE__);
	MemoryDeleter _(testFileSource);

	std::string testSrcDir(::dirname(testFileSource));

	return testSrcDir + "/" + relativePath;
}

}


HttpTest::HttpTest(TestServerMode mode)
	:
	fTestServer(mode)
{
}


HttpTest::~HttpTest()
{
}


void
HttpTest::setUp()
{
	CPPUNIT_ASSERT_EQUAL_MESSAGE(
		"Starting up test server",
		B_OK,
		fTestServer.StartIfNotRunning());
}


void
HttpTest::GetTest()
{
	BUrl testUrl(fTestServer.BaseUrl(), "/");
	BUrlContext* context = new BUrlContext();
	context->AcquireReference();

	std::string expectedResponseBody(
		"Path: /\r\n"
		"\r\n"
		"Headers:\r\n"
		"--------\r\n"
		"Host: 127.0.0.1:PORT\r\n"
		"Accept: */*\r\n"
		"Accept-Encoding: gzip\r\n"
		"Connection: close\r\n"
		"User-Agent: Services Kit (Haiku)\r\n");
	HttpHeaderMap expectedResponseHeaders;
	expectedResponseHeaders["Content-Encoding"] = "gzip";
	expectedResponseHeaders["Content-Length"] = "144";
	expectedResponseHeaders["Content-Type"] = "text/plain";
	expectedResponseHeaders["Date"] = "Sun, 09 Feb 2020 19:32:42 GMT";
	expectedResponseHeaders["Server"] = "Test HTTP Server for Haiku";

	TestListener listener(expectedResponseBody, expectedResponseHeaders);

	BHttpRequest request(testUrl, testUrl.Protocol() == "https");
	request.SetContext(context);
	request.SetListener(&listener);

	CPPUNIT_ASSERT(request.Run());
	while (request.IsRunning())
		snooze(1000);

	CPPUNIT_ASSERT_EQUAL(B_OK, request.Status());

	const BHttpResult& result
		= dynamic_cast<const BHttpResult&>(request.Result());
	CPPUNIT_ASSERT_EQUAL(200, result.StatusCode());
	CPPUNIT_ASSERT_EQUAL(BString("OK"), result.StatusText());

	CPPUNIT_ASSERT_EQUAL(144, result.Length());

	listener.Verify();

	CPPUNIT_ASSERT(!context->GetCookieJar().GetIterator().HasNext());
		// This page should not set cookies

	context->ReleaseReference();
}


void
HttpTest::ProxyTest()
{
	BUrl testUrl(fTestServer.BaseUrl(), "/user-agent");

	BUrlContext* c = new BUrlContext();
	c->AcquireReference();
	c->SetProxy("120.203.214.182", 83);

	BHttpRequest t(testUrl, testUrl.Protocol() == "https");
	t.SetContext(c);

	BUrlProtocolListener l;
	t.SetListener(&l);

	CPPUNIT_ASSERT(t.Run());

	while (t.IsRunning())
		snooze(1000);

	CPPUNIT_ASSERT_EQUAL(B_OK, t.Status());

	const BHttpResult& r = dynamic_cast<const BHttpResult&>(t.Result());
	CPPUNIT_ASSERT_EQUAL(200, r.StatusCode());
	CPPUNIT_ASSERT_EQUAL(BString("OK"), r.StatusText());
	CPPUNIT_ASSERT_EQUAL(42, r.Length());
		// Fixed size as we know the response format.
	CPPUNIT_ASSERT(!c->GetCookieJar().GetIterator().HasNext());
		// This page should not set cookies

	c->ReleaseReference();
}


void
HttpTest::UploadTest()
{
	std::string testFilePath = TestFilePath("testfile.txt");

	// The test server will echo the POST body back to us in the HTTP response,
	// so here we load it into memory so that we can compare to make sure that
	// the server received it.
	std::string fileContents;
	{
		std::ifstream inputStream(testFilePath);
		CPPUNIT_ASSERT(inputStream.is_open());
		fileContents = std::string(
			std::istreambuf_iterator<char>(inputStream),
			std::istreambuf_iterator<char>());
		CPPUNIT_ASSERT(!fileContents.empty());
	}

	std::string expectedResponseBody(
		"Path: /post\r\n"
		"\r\n"
		"Headers:\r\n"
		"--------\r\n"
		"Host: 127.0.0.1:PORT\r\n"
		"Accept: */*\r\n"
		"Accept-Encoding: gzip\r\n"
		"Connection: close\r\n"
		"User-Agent: Services Kit (Haiku)\r\n"
		"Content-Type: multipart/form-data; boundary=<<BOUNDARY-ID>>\r\n"
		"Content-Length: 1404\r\n"
		"\r\n"
		"Request body:\r\n"
		"-------------\r\n"
		"--<<BOUNDARY-ID>>\r\n"
		"Content-Disposition: form-data; name=\"_uploadfile\";"
		" filename=\"testfile.txt\"\r\n"
		"Content-Type: application/octet-stream\r\n"
		"\r\n"
		+ fileContents
		+ "\r\n"
		"--<<BOUNDARY-ID>>\r\n"
		"Content-Disposition: form-data; name=\"hello\"\r\n"
		"\r\n"
		"world\r\n"
		"--<<BOUNDARY-ID>>--\r\n"
		"\r\n");
	HttpHeaderMap expectedResponseHeaders;
	expectedResponseHeaders["Content-Encoding"] = "gzip";
	expectedResponseHeaders["Content-Length"] = "913";
	expectedResponseHeaders["Content-Type"] = "text/plain";
	expectedResponseHeaders["Date"] = "Sun, 09 Feb 2020 19:32:42 GMT";
	expectedResponseHeaders["Server"] = "Test HTTP Server for Haiku";
	TestListener listener(expectedResponseBody, expectedResponseHeaders);

	BUrl testUrl(fTestServer.BaseUrl(), "/post");

	BUrlContext context;

	BHttpRequest request(testUrl, testUrl.Protocol() == "https");
	request.SetContext(&context);
	request.SetListener(&listener);

	BHttpForm form;
	form.AddString("hello", "world");
	CPPUNIT_ASSERT_EQUAL(
		B_OK,
		form.AddFile("_uploadfile", BPath(testFilePath.c_str())));

	request.SetPostFields(form);

	CPPUNIT_ASSERT(request.Run());

	while (request.IsRunning())
		snooze(1000);

	CPPUNIT_ASSERT_EQUAL(B_OK, request.Status());

	const BHttpResult &result =
		dynamic_cast<const BHttpResult &>(request.Result());
	CPPUNIT_ASSERT_EQUAL(200, result.StatusCode());
	CPPUNIT_ASSERT_EQUAL(BString("OK"), result.StatusText());
	CPPUNIT_ASSERT_EQUAL(913, result.Length());

	listener.Verify();
}


void
HttpTest::AuthBasicTest()
{
	BUrlContext context;

	BUrl testUrl(fTestServer.BaseUrl(), "/auth/basic/walter/secret");

	std::string expectedResponseBody(
		"Path: /auth/basic/walter/secret\r\n"
		"\r\n"
		"Headers:\r\n"
		"--------\r\n"
		"Host: 127.0.0.1:PORT\r\n"
		"Accept: */*\r\n"
		"Accept-Encoding: gzip\r\n"
		"Connection: close\r\n"
		"User-Agent: Services Kit (Haiku)\r\n"
		"Referer: SCHEME://127.0.0.1:PORT/auth/basic/walter/secret\r\n"
		"Authorization: Basic d2FsdGVyOnNlY3JldA==\r\n");

	HttpHeaderMap expectedResponseHeaders;
	expectedResponseHeaders["Content-Encoding"] = "gzip";
	expectedResponseHeaders["Content-Length"] = "212";
	expectedResponseHeaders["Content-Type"] = "text/plain";
	expectedResponseHeaders["Date"] = "Sun, 09 Feb 2020 19:32:42 GMT";
	expectedResponseHeaders["Server"] = "Test HTTP Server for Haiku";
	expectedResponseHeaders["Www-Authenticate"] = "Basic realm=\"Fake Realm\"";

	SendAuthenticatedRequest(context, testUrl, expectedResponseBody,
		expectedResponseHeaders);

	CPPUNIT_ASSERT(!context.GetCookieJar().GetIterator().HasNext());
		// This page should not set cookies
}


void
HttpTest::AuthDigestTest()
{
	BUrlContext context;

	BUrl testUrl(fTestServer.BaseUrl(), "/auth/digest/walter/secret");

	std::string expectedResponseBody(
		"Path: /auth/digest/walter/secret\r\n"
		"\r\n"
		"Headers:\r\n"
		"--------\r\n"
		"Host: 127.0.0.1:PORT\r\n"
		"Accept: */*\r\n"
		"Accept-Encoding: gzip\r\n"
		"Connection: close\r\n"
		"User-Agent: Services Kit (Haiku)\r\n"
		"Referer: SCHEME://127.0.0.1:PORT/auth/digest/walter/secret\r\n"
		"Authorization: Digest username=\"walter\","
		" realm=\"user@shredder\","
		" nonce=\"f3a95f20879dd891a5544bf96a3e5518\","
		" algorithm=MD5,"
		" opaque=\"f0bb55f1221a51b6d38117c331611799\","
		" uri=\"/auth/digest/walter/secret\","
		" qop=auth,"
		" cnonce=\"60a3d95d286a732374f0f35fb6d21e79\","
		" nc=00000001,"
		" response=\"f4264de468aa1a91d81ac40fa73445f3\"\r\n"
		"Cookie: stale_after=never; fake=fake_value\r\n");

	HttpHeaderMap expectedResponseHeaders;
	expectedResponseHeaders["Content-Encoding"] = "gzip";
	expectedResponseHeaders["Content-Length"] = "403";
	expectedResponseHeaders["Content-Type"] = "text/plain";
	expectedResponseHeaders["Date"] = "Sun, 09 Feb 2020 19:32:42 GMT";
	expectedResponseHeaders["Server"] = "Test HTTP Server for Haiku";
	expectedResponseHeaders["Set-Cookie"] = "fake=fake_value; Path=/";
	expectedResponseHeaders["Www-Authenticate"]
		= "Digest realm=\"user@shredder\", "
		"nonce=\"f3a95f20879dd891a5544bf96a3e5518\", "
		"qop=\"auth\", "
		"opaque=f0bb55f1221a51b6d38117c331611799, "
		"algorithm=MD5, "
		"stale=FALSE";

	SendAuthenticatedRequest(context, testUrl, expectedResponseBody,
		expectedResponseHeaders);

	std::map<BString, BString> cookies;
	BNetworkCookieJar::Iterator iter
		= context.GetCookieJar().GetIterator();
	while (iter.HasNext()) {
		const BNetworkCookie* cookie = iter.Next();
		cookies[cookie->Name()] = cookie->Value();
	}
	CPPUNIT_ASSERT_EQUAL(2, cookies.size());
	CPPUNIT_ASSERT_EQUAL(BString("fake_value"), cookies["fake"]);
	CPPUNIT_ASSERT_EQUAL(BString("never"), cookies["stale_after"]);
}


/* static */ template<class T> void
HttpTest::_AddCommonTests(BString prefix, CppUnit::TestSuite& suite)
{
	T* test = new T();
	BThreadedTestCaller<T>* testCaller
		= new BThreadedTestCaller<T>(prefix.String(), test);

	testCaller->addThread("GetTest", &T::GetTest);
	testCaller->addThread("UploadTest", &T::UploadTest);
	testCaller->addThread("BasicAuthTest", &T::AuthBasicTest);
	testCaller->addThread("DigestAuthTest", &T::AuthDigestTest);

	suite.addTest(testCaller);
}


/* static */ void
HttpTest::AddTests(BTestSuite& parent)
{
	{
		CppUnit::TestSuite& suite = *new CppUnit::TestSuite("HttpTest");

		// HTTP + HTTPs
		_AddCommonTests<HttpTest>("HttpTest::", suite);

		// TODO: reaches out to some mysterious IP 120.203.214.182 which does
		// not respond anymore?
		//suite.addTest(new CppUnit::TestCaller<HttpTest>("HttpTest::ProxyTest",
		//	&HttpTest::ProxyTest));

		parent.addTest("HttpTest", &suite);
	}

	{
		CppUnit::TestSuite& suite = *new CppUnit::TestSuite("HttpsTest");

		// HTTP + HTTPs
		_AddCommonTests<HttpsTest>("HttpsTest::", suite);

		parent.addTest("HttpsTest", &suite);
	}
}


// # pragma mark - HTTPS


HttpsTest::HttpsTest()
	:
	HttpTest(TEST_SERVER_MODE_HTTPS)
{
}
