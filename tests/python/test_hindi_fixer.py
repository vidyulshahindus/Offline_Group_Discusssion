from eaglegd.core.HindiFeminineFixer import HindiFeminineFixer


def test_feminine_first_person() -> None:
    assert HindiFeminineFixer.apply("मैं सोचता हूँ") == "मैं सोचती हूँ"
    assert HindiFeminineFixer.apply("मैं कर सकता हूं") == "मैं कर सकती हूं"
    assert HindiFeminineFixer.apply("मैं आपको बताऊँगा") == "मैं आपको बताऊँगी"


def test_leaves_third_person() -> None:
    assert HindiFeminineFixer.apply("वह जाता है") == "वह जाता है"
    assert HindiFeminineFixer.apply("हम सोचते हैं") == "हम सोचते हैं"
