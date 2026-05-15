# pyright: reportAny=false, reportUnknownArgumentType=false

import asyncio
import json
from unittest.mock import Mock

import pytest
from tonapi import lite_api, ton_api

from tonlib import LiteClient, LocalError, RemoteError

MOCK_LOOP_PTR = 12345
MOCK_CLIENT_PTR = 67890
MOCK_RESPONSE_PTR = 11111
MOCK_CONTINUATION_ID = 42


@pytest.fixture
def mock_tonlib():
    tonlib = Mock()
    tonlib.lite_client_create = Mock(return_value=MOCK_CLIENT_PTR)
    tonlib.lite_client_destroy = Mock()
    tonlib.lite_client_is_error = Mock(return_value=False)
    tonlib.lite_client_get_error_code = Mock()
    tonlib.lite_client_get_error_message = Mock()
    tonlib.lite_client_request = Mock(return_value=MOCK_RESPONSE_PTR)
    tonlib.response_destroy = Mock()
    tonlib.response_await_ready = Mock(return_value=True)
    tonlib.response_await_suspend = Mock()
    tonlib.response_is_error = Mock(return_value=False)
    tonlib.response_get_error_code = Mock()
    tonlib.response_get_error_message = Mock()
    tonlib.response_get_response = Mock(return_value=b'{"@type": "liteServer.masterchainInfo"}')
    return tonlib


@pytest.fixture
def mock_event_loop():
    event_loop = Mock()
    event_loop.loop = MOCK_LOOP_PTR
    event_loop.create_awaitable_future = Mock()
    return event_loop


@pytest.fixture
def config():
    return ton_api.LiteClient_config(
        address="127.0.0.1:1234",
        server_public_key=ton_api.Pub_ed25519(key=b"\x00" * 32),
    )


@pytest.mark.asyncio
async def test_init_success(
    mock_tonlib: Mock, mock_event_loop: Mock, config: ton_api.LiteClient_config
):
    client = LiteClient(mock_tonlib, mock_event_loop, config)

    mock_tonlib.lite_client_create.assert_called_once_with(MOCK_LOOP_PTR, config.to_json().encode())
    mock_tonlib.lite_client_is_error.assert_called_once_with(MOCK_CLIENT_PTR)

    client.close()

    mock_tonlib.lite_client_destroy.assert_called_once_with(MOCK_CLIENT_PTR)


@pytest.mark.asyncio
async def test_init_error(
    mock_tonlib: Mock, mock_event_loop: Mock, config: ton_api.LiteClient_config
):
    mock_tonlib.lite_client_is_error = Mock(return_value=True)
    mock_tonlib.lite_client_get_error_code = Mock(return_value=500)
    mock_tonlib.lite_client_get_error_message = Mock(return_value=b"Connection failed")

    with pytest.raises(LocalError, match="Connection failed") as exc_info:
        _ = LiteClient(mock_tonlib, mock_event_loop, config)

    assert exc_info.value.code == 500
    assert exc_info.value.message == "Connection failed"

    mock_tonlib.lite_client_destroy.assert_called_once_with(MOCK_CLIENT_PTR)


@pytest.mark.asyncio
async def test_context_manager(
    mock_tonlib: Mock, mock_event_loop: Mock, config: ton_api.LiteClient_config
):
    with LiteClient(mock_tonlib, mock_event_loop, config):
        pass

    mock_tonlib.lite_client_destroy.assert_called_once_with(MOCK_CLIENT_PTR)


@pytest.mark.asyncio
async def test_close_idempotent(
    mock_tonlib: Mock, mock_event_loop: Mock, config: ton_api.LiteClient_config
):
    client = LiteClient(mock_tonlib, mock_event_loop, config)

    client.close()
    client.close()

    mock_tonlib.lite_client_destroy.assert_called_once()


@pytest.mark.asyncio
async def test_request_synchronous(
    mock_tonlib: Mock, mock_event_loop: Mock, config: ton_api.LiteClient_config
):
    mock_tonlib.response_await_ready = Mock(return_value=True)
    response_data = {"@type": "liteServer.masterchainInfo"}
    mock_tonlib.response_get_response = Mock(return_value=json.dumps(response_data).encode())

    with LiteClient(mock_tonlib, mock_event_loop, config) as client:
        request = lite_api.LiteServer_getMasterchainInfoRequest()
        result = await client.request(request)

        mock_tonlib.lite_client_request.assert_called_once_with(
            MOCK_CLIENT_PTR, request.to_json().encode()
        )

        mock_tonlib.response_await_ready.assert_called_once_with(MOCK_RESPONSE_PTR)
        mock_tonlib.response_await_suspend.assert_not_called()
        mock_tonlib.response_destroy.assert_called_once_with(MOCK_RESPONSE_PTR)

        assert result == response_data


@pytest.mark.asyncio
async def test_request_asynchronous(
    mock_tonlib: Mock, mock_event_loop: Mock, config: ton_api.LiteClient_config
):
    mock_tonlib.response_await_ready = Mock(return_value=False)
    response_data = {"@type": "liteServer.masterchainInfo"}
    mock_tonlib.response_get_response = Mock(return_value=json.dumps(response_data).encode())

    future = asyncio.get_event_loop().create_future()
    future.set_result(None)
    mock_event_loop.create_awaitable_future = Mock(return_value=(MOCK_CONTINUATION_ID, future))

    with LiteClient(mock_tonlib, mock_event_loop, config) as client:
        request = lite_api.LiteServer_getMasterchainInfoRequest()
        result = await client.request(request)

        mock_tonlib.response_await_suspend.assert_called_once_with(
            MOCK_RESPONSE_PTR, MOCK_CONTINUATION_ID
        )
        mock_tonlib.response_destroy.assert_called_once_with(MOCK_RESPONSE_PTR)

        assert result == response_data


@pytest.mark.asyncio
async def test_request_local_error(
    mock_tonlib: Mock, mock_event_loop: Mock, config: ton_api.LiteClient_config
):
    mock_tonlib.response_await_ready = Mock(return_value=True)
    mock_tonlib.response_is_error = Mock(return_value=True)
    mock_tonlib.response_get_error_code = Mock(return_value=404)
    mock_tonlib.response_get_error_message = Mock(return_value=b"Not found")

    with LiteClient(mock_tonlib, mock_event_loop, config) as client:
        request = lite_api.LiteServer_getMasterchainInfoRequest()

        with pytest.raises(LocalError, match="Not found") as exc_info:
            _ = await client.request(request)

        assert exc_info.value.code == 404
        assert exc_info.value.message == "Not found"

        mock_tonlib.response_destroy.assert_called_once_with(MOCK_RESPONSE_PTR)


@pytest.mark.asyncio
async def test_request_remote_error(
    mock_tonlib: Mock, mock_event_loop: Mock, config: ton_api.LiteClient_config
):
    mock_tonlib.response_await_ready = Mock(return_value=True)
    error_response = {
        "@type": "liteServer.error",
        "code": 651,
        "message": "block not found",
    }
    mock_tonlib.response_get_response = Mock(return_value=json.dumps(error_response).encode())

    with LiteClient(mock_tonlib, mock_event_loop, config) as client:
        request = lite_api.LiteServer_getMasterchainInfoRequest()

        with pytest.raises(RemoteError, match="block not found") as exc_info:
            _ = await client.request(request)

        assert exc_info.value.code == 651
        assert exc_info.value.message == "block not found"

        mock_tonlib.response_destroy.assert_called_once_with(MOCK_RESPONSE_PTR)
